/*!
 * \file   maptoolselect.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 */

#include "map/tools/maptoolselect.h"
#include "core/preferencesmanager.h"
#include "map/mapcanvas.h"
#include "swmmvisprojectwindow.h"
#include "map/mapextent.h"
#include "map/mapundostack.h"
#include "layers/gisvectorlayer.h"
#include "layers/swmmmodellayer.h"
#include "layers/swmmresultslayer.h"

#include "core/editgeometry.h"
#include "core/unitsystem.h"
#include "ui/widgets/attributepickermenu.h"
#include "ui/dialogs/typeconversionflow.h"

#include <openswmm/engine/openswmm_subcatchments.h>
#include <openswmm/engine/openswmm_nodes.h>
#include <openswmm/engine/openswmm_gages.h>

#include <QAction>
#include <QDebug>
#include <QElapsedTimer>
#include <QIcon>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QPushButton>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVariantMap>
#include <QWidget>

#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_edit.h>   // SWMM_ImpactReport — cascade lookup
#include <openswmm/engine/openswmm_engine.h>

#include <algorithm>

namespace {
// identifyAt "elementType" string → SWMMModelLayer kind bit. SWMM names
// are per-type namespaces, so selection carries the kind of the object
// that was actually hit. Unknown strings fall back to all kinds (legacy
// name-wide behaviour) rather than silently selecting nothing.
quint8 kindBitForElementType(const QString &t)
{
    if (t == QLatin1String("Node"))         return SWMMModelLayer::kKindNode;
    if (t == QLatin1String("Link"))         return SWMMModelLayer::kKindLink;
    if (t == QLatin1String("Subcatchment")) return SWMMModelLayer::kKindCatch;
    if (t == QLatin1String("RainGage"))     return SWMMModelLayer::kKindGage;
    return SWMMModelLayer::kKindAll;
}

// SWMMObjectRef::ObjectType → kind bit (context-menu paths hold typed refs).
quint8 kindBitForObjectType(int objectType)
{
    switch (objectType) {
    case SWMMObjectRef::Node:         return SWMMModelLayer::kKindNode;
    case SWMMObjectRef::Link:         return SWMMModelLayer::kKindLink;
    case SWMMObjectRef::Subcatchment: return SWMMModelLayer::kKindCatch;
    case SWMMObjectRef::RainGage:     return SWMMModelLayer::kKindGage;
    default:                          return SWMMModelLayer::kKindAll;
    }
}
} // namespace

OpenSWMMVisMapToolSelect::OpenSWMMVisMapToolSelect(MapCanvas *canvas, QObject *parent)
    : OpenSWMMVisMapTool(QStringLiteral("Select"), canvas, parent)
{
    // Slice V wiring — the hard-coded `m_pixelTol` / `m_dragThreshPx`
    // stay as fall-backs, but live usage prefers the PreferencesManager
    // value so Tools → Preferences takes effect without a restart.
    auto *prefs = PreferencesManager::instance();
    m_pixelTol      = prefs->clickTolerancePx();
    m_dragThreshPx  = prefs->dragThresholdPx();
    connect(prefs, &PreferencesManager::preferenceChanged, this,
            [this, prefs](const QString &grp, const QString &key) {
                if (grp != QStringLiteral("Selection")) return;
                if (key == QStringLiteral("ClickTolerancePx"))
                    m_pixelTol = prefs->clickTolerancePx();
                else if (key == QStringLiteral("DragThresholdPx"))
                    m_dragThreshPx = prefs->dragThresholdPx();
            });
}

QCursor OpenSWMMVisMapToolSelect::cursor() const
{
    return Qt::ArrowCursor;
}

int OpenSWMMVisMapToolSelect::pixelTolerance() const { return m_pixelTol; }

void OpenSWMMVisMapToolSelect::setPixelTolerance(int pixels)
{
    if (m_pixelTol != pixels)
    {
        m_pixelTol = pixels;
        emit pixelToleranceChanged(pixels);
    }
}

QColor OpenSWMMVisMapToolSelect::rubberBandColor() const { return m_rubberColor; }

void OpenSWMMVisMapToolSelect::setRubberBandColor(const QColor &color)
{
    if (m_rubberColor != color)
    {
        m_rubberColor = color;
        emit rubberBandColorChanged(color);
    }
}

void OpenSWMMVisMapToolSelect::activate()
{
    m_dragging = false;
    OpenSWMMVisMapTool::activate();
}

void OpenSWMMVisMapToolSelect::deactivate()
{
    m_dragging = false;
    clearEditMode();
    OpenSWMMVisMapTool::deactivate();
}

void OpenSWMMVisMapToolSelect::mousePressEvent(QMouseEvent *event)
{
    // ── Edit sub-mode intercept ───────────────────────────────────────────
    if (m_editKind != EditKind::None)
    {
        if (event->button() == Qt::LeftButton)
        {
            const int h = hitTestEditHandle(event->pos());
            if (h >= 0)
            {
                // Starting a group drag when multiple handles are selected
                // and this handle is one of them; otherwise select just this
                // one and do a single-handle drag.
                const bool groupDrag = m_editSelectedHandles.size() > 1
                                       && m_editSelectedHandles.contains(h)
                                       && m_editKind != EditKind::Node;
                if (!groupDrag)
                {
                    m_editSelectedHandles.clear();
                    m_editSelectedHandles.insert(h);
                }

                m_editDragging   = true;
                m_editDragHandle = h;

                // Record the pre-drag position so snap can exclude this exact
                // vertex while still snapping to every other vertex on the same
                // (or any other) object.
                m_editDragOrigPt = m_editHandles[h];

                // Snapshot for delta tracking (group drag) and undo (node).
                double mgx = 0.0, mgy = 0.0;
                toMapCoords(event->pos().x(), event->pos().y(), mgx, mgy);
                m_editGroupDragPrev = QPointF(mgx, mgy);

                if (m_editKind == EditKind::Node)
                {
                    m_editNodeOrigX = m_editHandles[h].x();
                    m_editNodeOrigY = m_editHandles[h].y();
                }
                else if (m_editKind == EditKind::Subcatch)
                {
                    m_editCentroidPrev = m_editHandles[0];
                }
                event->accept();
                return;
            }
            // No handle hit: begin potential rubber-band vertex selection.
            m_editPressedEmpty   = true;
            m_editRubberbanding  = false;
            m_editRubberStart    = event->pos();
            m_editRubberCurrent  = event->pos();
            m_editSelectedHandles.clear();
            event->accept();
            return;
        }
        else if (event->button() == Qt::RightButton
                 && m_editLayer && m_editSoaIdx >= 0
                 && (m_editKind == EditKind::Link || m_editKind == EditKind::Subcatch))
        {
            const int h = hitTestEditHandle(event->pos());

            if (m_editKind == EditKind::Link)
            {
                // Link: right-click handle → delete vertex/vertices.
                //       right-click segment → insert vertex.
                QMenu menu;
                if (h >= 0)
                {
                    // Multi-selection: offer batch delete when this handle is selected.
                    const bool multiSel = m_editSelectedHandles.size() > 1
                                         && m_editSelectedHandles.contains(h);
                    if (multiSel)
                    {
                        QAction *delSel = menu.addAction(
                            tr("Delete %1 selected vertices").arg(m_editSelectedHandles.size()));
                        menu.addSeparator();
                        QAction *delOne = menu.addAction(tr("Delete this vertex"));
                        const auto chosen = menu.exec(event->globalPosition().toPoint());
                        if (chosen == delSel)
                            deleteSelectedEditHandles();
                        else if (chosen == delOne)
                            commitLinkDrag(EditGeometry::removedAt(m_editHandles, h));
                    }
                    else
                    {
                        QAction *del = menu.addAction(tr("Delete vertex"));
                        if (menu.exec(event->globalPosition().toPoint()) == del)
                            commitLinkDrag(EditGeometry::removedAt(m_editHandles, h));
                    }
                }
                else
                {
                    double mx = 0.0, my = 0.0;
                    toMapCoords(event->pos().x(), event->pos().y(), mx, my);
                    const QVector<QPointF> full =
                        m_editLayer->cachedLinkPolyline(m_editSoaIdx);
                    int seg = -1;
                    QPointF proj;
                    const double d =
                        EditGeometry::distanceToPolyline(full, {mx, my}, &seg, &proj);
                    const double px2m = m_canvas && m_canvas->width() > 0
                        ? m_canvas->extent().width() / m_canvas->width() : 1.0;
                    if (d <= 10.0 * px2m && seg >= 0)
                    {
                        QAction *ins = menu.addAction(tr("Insert vertex here"));
                        if (menu.exec(event->globalPosition().toPoint()) == ins)
                        {
                            const int idx = std::clamp(seg, 0,
                                                       (int)m_editHandles.size());
                            commitLinkDrag(
                                EditGeometry::insertedAt(m_editHandles, idx, proj));
                        }
                    }
                }
            }
            else // EditKind::Subcatch
            {
                // Subcatch: right-click a vertex handle → delete (min 3 vertices).
                //           right-click a polygon edge   → insert vertex.
                // Handle 0 is the centroid (not a polygon vertex) — skip it.
                QMenu menu;
                if (h > 0)
                {
                    const bool multiSel = m_editSelectedHandles.size() > 1
                                         && m_editSelectedHandles.contains(h);
                    if (multiSel)
                    {
                        // Count how many polygon vertices will actually be removed
                        // (ignore centroid handle 0 if somehow selected).
                        int numVerts = 0;
                        for (int s : std::as_const(m_editSelectedHandles))
                            if (s > 0) ++numVerts;
                        QAction *delSel = menu.addAction(
                            tr("Delete %1 selected vertices").arg(numVerts));
                        menu.addSeparator();
                        const bool canDeleteOne = m_editSubcatchVerts.size() > 3;
                        QAction *delOne = menu.addAction(tr("Delete this vertex"));
                        delOne->setEnabled(canDeleteOne);
                        if (!canDeleteOne)
                            delOne->setToolTip(tr("A subcatchment must have at least 3 vertices"));
                        const auto chosen = menu.exec(event->globalPosition().toPoint());
                        if (chosen == delSel)
                            deleteSelectedEditHandles();
                        else if (chosen == delOne && canDeleteOne)
                        {
                            const int vi = h - 1;
                            QVector<QPointF> newVerts =
                                EditGeometry::removedAt(m_editSubcatchVerts, vi);
                            commitSubcatchDrag(newVerts);
                            m_editSubcatchVerts = newVerts;
                            double cx = 0.0, cy = 0.0;
                            for (const QPointF &p : newVerts) { cx += p.x(); cy += p.y(); }
                            cx /= newVerts.size(); cy /= newVerts.size();
                            m_editHandles.clear();
                            m_editHandles.reserve(1 + newVerts.size());
                            m_editHandles.append(QPointF(cx, cy));
                            m_editHandles.append(newVerts);
                        }
                    }
                    else
                    {
                    const bool canDelete = m_editSubcatchVerts.size() > 3;
                    QAction *del = menu.addAction(tr("Delete vertex"));
                    del->setEnabled(canDelete);
                    if (!canDelete)
                        del->setToolTip(tr("A subcatchment must have at least 3 vertices"));
                    if (menu.exec(event->globalPosition().toPoint()) == del && canDelete)
                    {
                        // h is 1-based (handle 0 = centroid), vi is 0-based vertex index
                        const int vi = h - 1;
                        QVector<QPointF> newVerts =
                            EditGeometry::removedAt(m_editSubcatchVerts, vi);
                        commitSubcatchDrag(newVerts);
                        // Recompute handles from the committed vertices
                        m_editSubcatchVerts = newVerts;
                        double cx = 0.0, cy = 0.0;
                        for (const QPointF &p : newVerts) { cx += p.x(); cy += p.y(); }
                        cx /= newVerts.size(); cy /= newVerts.size();
                        m_editHandles.clear();
                        m_editHandles.reserve(1 + newVerts.size());
                        m_editHandles.append(QPointF(cx, cy));
                        m_editHandles.append(newVerts);
                    }
                    } // closes else (non-multiSel single delete)
                }     // closes if (h > 0)
                else
                {
                    // Right-click on the polygon body/edge — insert a vertex on the
                    // nearest polygon edge.  Close the polygon by appending the first
                    // vertex so distanceToPolyline tests the closing edge too.
                    if (m_editSubcatchVerts.size() >= 2)
                    {
                        double mx = 0.0, my = 0.0;
                        toMapCoords(event->pos().x(), event->pos().y(), mx, my);

                        QVector<QPointF> closed = m_editSubcatchVerts;
                        closed.append(m_editSubcatchVerts.first()); // close the loop

                        int seg = -1;
                        QPointF proj;
                        const double d = EditGeometry::distanceToPolyline(
                            closed, {mx, my}, &seg, &proj);

                        const double px2m = m_canvas && m_canvas->width() > 0
                            ? m_canvas->extent().width() / m_canvas->width() : 1.0;
                        if (d <= 10.0 * px2m && seg >= 0)
                        {
                            QAction *ins = menu.addAction(tr("Insert vertex here"));
                            if (menu.exec(event->globalPosition().toPoint()) == ins)
                            {
                                // seg is an index into `closed`; since closed has one
                                // extra point at the end, a hit on the closing edge
                                // (seg == verts.size()-1) means we insert AFTER the last
                                // vertex, which wraps to the front — correct for a
                                // polygon.  clamp to [0, verts.size()].
                                const int insertIdx = std::clamp(
                                    seg + 1, 0,
                                    static_cast<int>(m_editSubcatchVerts.size()));
                                QVector<QPointF> newVerts =
                                    EditGeometry::insertedAt(
                                        m_editSubcatchVerts, insertIdx, proj);
                                commitSubcatchDrag(newVerts);
                                // Rebuild handles
                                m_editSubcatchVerts = newVerts;
                                double cx = 0.0, cy = 0.0;
                                for (const QPointF &p : newVerts)
                                    { cx += p.x(); cy += p.y(); }
                                cx /= newVerts.size(); cy /= newVerts.size();
                                m_editHandles.clear();
                                m_editHandles.reserve(1 + newVerts.size());
                                m_editHandles.append(QPointF(cx, cy));
                                m_editHandles.append(newVerts);
                            }
                        }
                    }
                }
            }
            event->accept();
            return;
        }
    }

    // ── Normal select-tool behaviour ─────────────────────────────────────
    if (event->button() == Qt::LeftButton)
    {
        m_dragging     = false;
        m_startPixel   = event->pos();
        m_currentPixel = event->pos();
    }
    else if (event->button() == Qt::RightButton)
    {
        showContextMenu(event->pos());
        event->accept();
    }
}

void OpenSWMMVisMapToolSelect::mouseMoveEvent(QMouseEvent *event)
{
    // ── Edit sub-mode drag ────────────────────────────────────────────────
    if (m_editDragging && m_editDragHandle >= 0
        && m_editDragHandle < m_editHandles.size())
    {
        double mx = 0.0, my = 0.0;
        toMapCoords(event->pos().x(), event->pos().y(), mx, my);

        const bool groupDrag = m_editSelectedHandles.size() > 1
                               && m_editSelectedHandles.contains(m_editDragHandle)
                               && m_editKind != EditKind::Node;
        const bool centroidTranslate =
            m_editKind == EditKind::Subcatch && m_editDragHandle == 0;

        // Snap single-handle drags to the nearest node or link interior vertex.
        // Group drags and centroid-translate are excluded because their delta
        // is relative; snapping one absolute position would skew the whole group.
        double sx = mx, sy = my;
        m_snapping = false;
        if (!groupDrag && !centroidTranslate && m_editLayer) {
            // Convert kSnapRadiusPx pixels into map units at the current zoom.
            double sx1, sy1, sx2, sy2;
            toMapCoords(0, 0, sx1, sy1);
            toMapCoords(kSnapRadiusPx, 0, sx2, sy2);
            const double mapRadius = std::abs(sx2 - sx1);
            QPointF snapPt;
            if (m_editLayer->snapNearestPoint(mx, my, mapRadius, snapPt, m_editDragOrigPt)) {
                sx = snapPt.x();
                sy = snapPt.y();
                m_snapPt  = snapPt;
                m_snapping = true;
            }
        }

        if (groupDrag)
        {
            const double dx = mx - m_editGroupDragPrev.x();
            const double dy = my - m_editGroupDragPrev.y();
            m_editGroupDragPrev = QPointF(mx, my);
            applyGroupDragDelta(dx, dy);
        }
        else if (centroidTranslate)
        {
            // Centroid handle → translate entire polygon.
            const double dx = mx - m_editCentroidPrev.x();
            const double dy = my - m_editCentroidPrev.y();
            m_editCentroidPrev = QPointF(mx, my);
            for (QPointF &v : m_editSubcatchVerts) v += QPointF(dx, dy);
            m_editHandles[0] = QPointF(mx, my);
            for (int i = 0; i < m_editSubcatchVerts.size(); ++i)
                m_editHandles[i + 1] = m_editSubcatchVerts[i];
        }
        else if (m_editKind == EditKind::Subcatch && m_editDragHandle > 0)
        {
            const int vi = m_editDragHandle - 1;
            m_editSubcatchVerts[vi] = QPointF(sx, sy);
            m_editHandles[m_editDragHandle] = QPointF(sx, sy);
            double cx = 0.0, cy = 0.0;
            for (const QPointF &p : m_editSubcatchVerts) { cx += p.x(); cy += p.y(); }
            cx /= m_editSubcatchVerts.size(); cy /= m_editSubcatchVerts.size();
            m_editHandles[0] = QPointF(cx, cy);
        }
        else
        {
            m_editHandles[m_editDragHandle] = QPointF(sx, sy);
        }

        if (m_editKind == EditKind::Node && m_editLayer && m_editSoaIdx >= 0)
            m_editLayer->previewNodeMove(m_editSoaIdx, sx, sy);

        if (m_canvas)
            m_canvas->invalidate(MapCanvas::Overlay | MapCanvas::Scene,
                                 QStringLiteral("select-edit-drag"));
        return;
    }

    // ── Edit rubber-band build ────────────────────────────────────────────
    if (m_editPressedEmpty && (event->buttons() & Qt::LeftButton))
    {
        const QPoint delta = event->pos() - m_editRubberStart;
        if (!m_editRubberbanding
            && (std::abs(delta.x()) > m_dragThreshPx
             || std::abs(delta.y()) > m_dragThreshPx))
            m_editRubberbanding = true;

        if (m_editRubberbanding)
        {
            m_editRubberCurrent = event->pos();
            if (m_canvas)
                m_canvas->invalidate(MapCanvas::Overlay,
                                     QStringLiteral("edit-rubberband"));
        }
        return;
    }

    // ── Rubber-band drag ──────────────────────────────────────────────────
    if (event->buttons() & Qt::LeftButton)
    {
        m_currentPixel = event->pos();
        QPoint delta   = m_currentPixel - m_startPixel;

        if (!m_dragging
            && (std::abs(delta.x()) > m_dragThreshPx
             || std::abs(delta.y()) > m_dragThreshPx))
            m_dragging = true;

        if (m_dragging && m_canvas)
            m_canvas->invalidate(MapCanvas::Overlay,
                                 QStringLiteral("select-tool-rubberband"));
        return;
    }

    // ── Hover cursor feedback ─────────────────────────────────────────────
    if (!m_canvas) return;

    // Show a size-all cursor when hovering over an edit handle.
    if (m_editKind != EditKind::None)
    {
        if (hitTestEditHandle(event->pos()) >= 0)
        {
            m_canvas->setCursor(Qt::SizeAllCursor);
            return;
        }
        m_canvas->setCursor(cursor());
        return;
    }

    double mx = 0.0, my = 0.0;
    toMapCoords(event->pos().x(), event->pos().y(), mx, my);
    double mx2 = 0.0, my2 = 0.0;
    toMapCoords(event->pos().x() + 12, event->pos().y() + 12, mx2, my2);
    const double tol = std::max(std::abs(mx2 - mx), std::abs(my2 - my));

    bool hovering = false;
    for (OpenSWMMVisLayer *l : m_canvas->layers()) {
        if (!l->isVisible()) continue;
        auto *sl = qobject_cast<SWMMModelLayer *>(l);
        if (!sl) continue;
        if (sl->pickAt(mx, my, tol).valid) { hovering = true; break; }
    }
    m_canvas->setCursor(hovering ? Qt::PointingHandCursor : cursor());
}

void OpenSWMMVisMapToolSelect::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;

    // ── Complete edit rubber-band selection ───────────────────────────────
    if (m_editRubberbanding)
    {
        m_editRubberbanding = false;
        m_editPressedEmpty  = false;
        selectHandlesInRect(QRect(m_editRubberStart, event->pos()).normalized());
        if (m_canvas)
            m_canvas->invalidate(MapCanvas::Overlay,
                                 QStringLiteral("edit-rubberband-commit"));
        return;
    }

    // ── Click on empty space → exit edit mode ─────────────────────────────
    if (m_editPressedEmpty)
    {
        m_editPressedEmpty = false;
        clearEditMode();
        return;
    }

    // ── Commit edit handle drag ───────────────────────────────────────────
    if (m_editDragging)
    {
        m_editDragging   = false;
        const int h      = m_editDragHandle;
        m_editDragHandle = -1;

        if (h >= 0 && m_editLayer && m_editSoaIdx >= 0)
        {
            if (m_editKind == EditKind::Node)
                commitNodeDrag(m_editHandles[0].x(), m_editHandles[0].y());
            else if (m_editKind == EditKind::Link)
                commitLinkDrag(m_editHandles);
            else if (m_editKind == EditKind::Subcatch)
                commitSubcatchDrag(m_editSubcatchVerts);
        }
        return;
    }

    // ── Normal rubber-band / click-select ─────────────────────────────────
    if (m_dragging)
    {
        QRect rect = QRect(m_startPixel, event->pos()).normalized();
        selectInRect(rect, event->modifiers());
    }
    else
    {
        selectAtPoint(event->pos(), event->modifiers());
    }

    m_dragging = false;

    if (m_canvas)
        m_canvas->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                             QStringLiteral("select-tool-commit"));
}

void OpenSWMMVisMapToolSelect::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape && m_editKind != EditKind::None)
    {
        // Cancel any in-flight drag, then exit edit mode.
        if (m_editDragging && m_editKind == EditKind::Node
            && m_editLayer && m_editSoaIdx >= 0)
            m_editLayer->previewNodeMove(m_editSoaIdx,
                                         m_editNodeOrigX, m_editNodeOrigY);
        m_editDragging   = false;
        m_editDragHandle = -1;
        clearEditMode();
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
    {
        // In edit mode with vertex handles selected → delete those vertices.
        if (m_editKind != EditKind::None && !m_editSelectedHandles.isEmpty())
        {
            deleteSelectedEditHandles();
            event->accept();
            return;
        }
        deleteSelectedObjects();
        return;
    }

    if (event->key() == Qt::Key_Escape && m_canvas)
    {
        for (OpenSWMMVisLayer *l : m_canvas->layers())
        {
            if (auto *vl = qobject_cast<GISVectorLayer *>(l))
            {
                QSet<long long> empty;
                vl->setSelectedFeatureIds(empty);
                emit selectionChanged(vl);
            }
        }
        m_canvas->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                             QStringLiteral("select-tool-clear"));
    }
}

void OpenSWMMVisMapToolSelect::deleteSelectedObjects()
{
    if (!m_canvas) return;

    SWMMModelLayer *sl = nullptr;
    for (OpenSWMMVisLayer *l : m_canvas->layers()) {
        if ((sl = qobject_cast<SWMMModelLayer *>(l))) break;
    }
    if (!sl) return;

    const QVector<SWMMModelLayer::SelectedElement> selected = sl->selectedElements();
    if (selected.isEmpty()) return;

    // Classify each selected object from its TYPED kind bits — SWMM names
    // are per-type namespaces, so classifying by name (findObjectLocation,
    // a single-keyed hash) could delete a same-named object of the wrong
    // kind. Each kind bit is validated against the engine/SoA so stale or
    // legacy all-kind entries never enqueue phantom deletes.
    struct ObjInfo { QString name; DeleteObjectCommand::TargetKind kind; };
    QList<ObjInfo> toDelete;
    QSet<QString> nodeNames;   // names of selected (existing) nodes
    QSet<QString> skipLinks;   // link names that will cascade-delete

    SWMM_Engine eng = sl->engine();

    // First pass: identify nodes and their cascade links.
    for (const auto &e : selected) {
        if (!(e.kinds & SWMMModelLayer::kKindNode)) continue;
        const int ni = sl->nodeIndex(e.name);
        if (ni < 0) continue;
        nodeNames.insert(e.name);
        // Find cascade links. This loop is nested inside the per-node loop,
        // so the old full scan was O(K*L) with two engine getters per link:
        // deleting 1000 selected nodes from an all-pipes model meant ~562
        // MILLION engine calls before a single object was removed. The
        // engine already knows the answer — ask it once per node.
        // Nothing is deleted yet, so every reported index is still live.
        SWMM_ImpactReport report{};
        if (swmm_node_analyze_impact(eng, ni, &report) == 0) {
            for (int i = 0; i < report.n_entries; ++i) {
                const SWMM_ImpactEntry &en = report.entries[i];
                if (en.obj_type != SWMM_REF_LINK || !en.cascaded) continue;
                if (const char *lid = swmm_link_id(eng, en.obj_idx))
                    skipLinks.insert(QString::fromUtf8(lid));
            }
        }
        swmm_impact_report_free(&report);
    }

    // Second pass: build the delete list, excluding cascade-handled links.
    for (const auto &e : selected) {
        const QByteArray id = e.name.toUtf8();
        if ((e.kinds & SWMMModelLayer::kKindNode) && nodeNames.contains(e.name))
            toDelete.append({e.name, DeleteObjectCommand::DeleteNode});
        if ((e.kinds & SWMMModelLayer::kKindLink)
            && !skipLinks.contains(e.name)
            && sl->linkIndex(e.name) >= 0)
            toDelete.append({e.name, DeleteObjectCommand::DeleteLink});
        if ((e.kinds & SWMMModelLayer::kKindGage)
            && eng && swmm_gage_index(eng, id.constData()) >= 0)
            toDelete.append({e.name, DeleteObjectCommand::DeleteGage});
        if ((e.kinds & SWMMModelLayer::kKindCatch)
            && eng && swmm_subcatch_index(eng, id.constData()) >= 0)
            toDelete.append({e.name, DeleteObjectCommand::DeleteSubcatch});
    }

    if (toDelete.isEmpty()) return;

    // Virtual junction: the default delete RE-FUSES the two conduits back
    // into one (the node only exists as a split); cascade delete of node +
    // both conduits remains available as the explicit alternative.
    if (toDelete.size() == 1
        && toDelete.first().kind == DeleteObjectCommand::DeleteNode) {
        const int ni = sl->nodeIndex(toDelete.first().name);
        if (ni >= 0 && sl->nodeIsVirtual(ni)) {
            auto *widget = qobject_cast<QWidget *>(m_canvas);
            QMessageBox box(widget);
            box.setWindowTitle(QObject::tr("Delete Virtual Junction"));
            box.setText(QObject::tr("\"%1\" is a virtual junction.")
                            .arg(toDelete.first().name));
            box.setInformativeText(QObject::tr(
                "Re-fuse merges its two conduits back into one (the upstream "
                "conduit's name survives). Delete removes the node and both "
                "conduits."));
            auto *fuseBtn = box.addButton(QObject::tr("Re-fuse Conduits"),
                                          QMessageBox::AcceptRole);
            auto *delBtn  = box.addButton(QObject::tr("Delete Node && Conduits"),
                                          QMessageBox::DestructiveRole);
            box.addButton(QMessageBox::Cancel);
            box.setDefaultButton(fuseBtn);
            box.exec();

            if (box.clickedButton() == fuseBtn) {
                sl->setSelectedElementNames({});
                emit selectionChanged(sl);
                auto *cmd = new FuseVirtualJunctionCommand(
                    sl, toDelete.first().name, m_canvas);
                if (!cmd->valid()) { delete cmd; return; }
                if (m_canvas->undoStack())
                    m_canvas->undoStack()->push(cmd);
                else
                    delete cmd;
                return;
            }
            if (box.clickedButton() != delBtn)
                return;   // cancelled
            // Explicit cascade delete chosen — skip the generic confirm.
            sl->setSelectedElementNames({});
            emit selectionChanged(sl);
            auto *macro = new BulkEditCommand(sl, QObject::tr("Delete Objects"));
            new DeleteObjectCommand(sl, toDelete.first().name,
                                    DeleteObjectCommand::DeleteNode,
                                    m_canvas, macro);
            if (m_canvas->undoStack())
                m_canvas->undoStack()->push(macro);
            else
                delete macro;
            return;
        }
    }

    // Deleting one conduit of a virtual-junction pair breaks the pair rule.
    // Offer re-fusing instead, or demote the node to a regular junction and
    // proceed with the deletion.
    if (toDelete.size() == 1
        && toDelete.first().kind == DeleteObjectCommand::DeleteLink && eng) {
        const int li = sl->linkIndex(toDelete.first().name);
        int vjNode = -1;
        if (li >= 0) {
            int n1 = -1, n2 = -1;
            swmm_link_get_from_node(eng, li, &n1);
            swmm_link_get_to_node(eng, li, &n2);
            if (n1 >= 0 && sl->nodeIsVirtual(n1)) vjNode = n1;
            if (n2 >= 0 && sl->nodeIsVirtual(n2)) vjNode = n2;
        }
        if (vjNode >= 0) {
            const QString vjName = QString::fromUtf8(swmm_node_id(eng, vjNode));
            auto *widget = qobject_cast<QWidget *>(m_canvas);
            QMessageBox box(widget);
            box.setWindowTitle(QObject::tr("Conduit Belongs to a Virtual Junction"));
            box.setText(QObject::tr("\"%1\" is one of the two conduits of "
                                    "virtual junction \"%2\".")
                            .arg(toDelete.first().name, vjName));
            box.setInformativeText(QObject::tr(
                "Re-fuse merges the pair back into one conduit. Delete removes "
                "this conduit and demotes \"%1\" to a regular junction.")
                    .arg(vjName));
            auto *fuseBtn = box.addButton(QObject::tr("Re-fuse Conduits"),
                                          QMessageBox::AcceptRole);
            auto *delBtn  = box.addButton(QObject::tr("Delete Conduit"),
                                          QMessageBox::DestructiveRole);
            box.addButton(QMessageBox::Cancel);
            box.setDefaultButton(fuseBtn);
            box.exec();

            if (box.clickedButton() == fuseBtn) {
                sl->setSelectedElementNames({});
                emit selectionChanged(sl);
                auto *cmd = new FuseVirtualJunctionCommand(sl, vjName, m_canvas);
                if (!cmd->valid()) { delete cmd; return; }
                if (m_canvas->undoStack())
                    m_canvas->undoStack()->push(cmd);
                else
                    delete cmd;
                return;
            }
            if (box.clickedButton() != delBtn)
                return;   // cancelled
            // Demote the node so the model stays valid, then fall through to
            // the standard confirm + delete path.
            sl->applySetVirtual(vjName, false);
        }
    }

    // Confirm.
    const int n = toDelete.size();
    const QString msg = (n == 1)
        ? QObject::tr("Delete \"%1\"? This cannot be undone by simple Ctrl+Z "
                      "if other edits follow.").arg(toDelete.first().name)
        : QObject::tr("Delete %1 selected objects?").arg(n);

    auto *widget = qobject_cast<QWidget *>(m_canvas);
    const auto btn = QMessageBox::question(
        widget, QObject::tr("Confirm Delete"), msg,
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (btn != QMessageBox::Yes) return;

    // Clear selection before deletion so stale names don't linger.
    sl->setSelectedElementNames({});
    emit selectionChanged(sl);

    // Group all deletes under one parent so Ctrl+Z undoes them together, and
    // so the whole batch shares a single cache rebuild in both directions.
    auto *macro = new BulkEditCommand(sl, QObject::tr("Delete Objects"));
    for (const ObjInfo &obj : toDelete)
        new DeleteObjectCommand(sl, obj.name, obj.kind, m_canvas, macro);

    if (m_canvas->undoStack())
        m_canvas->undoStack()->push(macro);
    else
        delete macro;
}

void OpenSWMMVisMapToolSelect::paint(QPainter *painter,
                                  const MapExtent &,
                                  const SpatialReferenceSystem *)
{
    // ── Rubber-band ───────────────────────────────────────────────────────
    if (m_dragging)
    {
        QRect rect = QRect(m_startPixel, m_currentPixel).normalized();
        painter->save();
        painter->setPen(QPen(m_rubberColor.darker(130), 1));
        painter->setBrush(m_rubberColor);
        painter->drawRect(rect);
        painter->restore();
    }

    // ── Edit rubber-band (vertex selection) ───────────────────────────────
    if (m_editRubberbanding)
    {
        QRect rect = QRect(m_editRubberStart, m_editRubberCurrent).normalized();
        painter->save();
        painter->setPen(QPen(QColor(255, 140, 0, 200), 1, Qt::DashLine));
        painter->setBrush(QColor(255, 140, 0, 35));
        painter->drawRect(rect);
        painter->restore();
    }

    // ── Edit mode handles ─────────────────────────────────────────────────
    if (m_editKind == EditKind::None || !m_editLayer || m_editSoaIdx < 0)
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);

    if (m_editKind == EditKind::Link)
    {
        // Highlight the full link polyline.
        const QVector<QPointF> full =
            m_editLayer->cachedLinkPolyline(m_editSoaIdx);
        if (full.size() >= 2)
        {
            QPainterPath path;
            int px = 0, py = 0;
            toPixelCoords(full[0].x(), full[0].y(), px, py);
            path.moveTo(px, py);
            for (int i = 1; i < full.size(); ++i)
            {
                toPixelCoords(full[i].x(), full[i].y(), px, py);
                path.lineTo(px, py);
            }
            QPen hi(QColor(0, 120, 255, 200), 2.5);
            hi.setCosmetic(true);
            painter->setPen(hi);
            painter->setBrush(Qt::NoBrush);
            painter->drawPath(path);
        }

        // Interior vertex handles — squares; filled blue when selected.
        for (int i = 0; i < m_editHandles.size(); ++i)
        {
            const bool sel = m_editSelectedHandles.contains(i);
            painter->setBrush(sel ? QColor(0, 80, 200) : QColor(Qt::white));
            painter->setPen(QPen(QColor(0, 80, 200), 1.5));
            int px = 0, py = 0;
            toPixelCoords(m_editHandles[i].x(), m_editHandles[i].y(), px, py);
            painter->drawRect(px - kEditHandlePx, py - kEditHandlePx,
                              kEditHandlePx * 2, kEditHandlePx * 2);
        }
    }
    else if (m_editKind == EditKind::Node && !m_editHandles.isEmpty())
    {
        // Node handle — circle with crosshair.
        int px = 0, py = 0;
        toPixelCoords(m_editHandles[0].x(), m_editHandles[0].y(), px, py);

        const int r = kEditHandlePx + 3;
        painter->setBrush(QColor(255, 255, 255, 220));
        painter->setPen(QPen(QColor(0, 80, 200), 2.0));
        painter->drawEllipse(px - r, py - r, r * 2, r * 2);

        painter->setPen(QPen(QColor(0, 80, 200), 1.5));
        painter->drawLine(px - r + 3, py, px + r - 3, py);
        painter->drawLine(px, py - r + 3, px, py + r - 3);
    }
    else if (m_editKind == EditKind::Subcatch
             && m_editHandles.size() >= 1)
    {
        // Draw polygon outline.
        if (m_editSubcatchVerts.size() >= 2)
        {
            QPainterPath poly;
            int px = 0, py = 0;
            toPixelCoords(m_editSubcatchVerts[0].x(),
                          m_editSubcatchVerts[0].y(), px, py);
            poly.moveTo(px, py);
            for (int i = 1; i < m_editSubcatchVerts.size(); ++i)
            {
                toPixelCoords(m_editSubcatchVerts[i].x(),
                              m_editSubcatchVerts[i].y(), px, py);
                poly.lineTo(px, py);
            }
            poly.closeSubpath();
            QPen hi(QColor(0, 120, 255, 200), 2.0);
            hi.setCosmetic(true);
            painter->setPen(hi);
            painter->setBrush(QColor(0, 120, 255, 25));
            painter->drawPath(poly);
        }

        // Vertex handles — circles (indices 1..N); filled blue when selected.
        for (int i = 1; i < m_editHandles.size(); ++i)
        {
            const bool sel = m_editSelectedHandles.contains(i);
            painter->setBrush(sel ? QColor(0, 80, 200) : QColor(Qt::white));
            painter->setPen(QPen(QColor(0, 80, 200), 1.5));
            int px = 0, py = 0;
            toPixelCoords(m_editHandles[i].x(), m_editHandles[i].y(), px, py);
            painter->drawEllipse(px - kEditHandlePx, py - kEditHandlePx,
                                 kEditHandlePx * 2, kEditHandlePx * 2);
        }

        // Centroid handle — filled square (index 0, move-all).
        {
            int px = 0, py = 0;
            toPixelCoords(m_editHandles[0].x(), m_editHandles[0].y(), px, py);
            const int hs = kEditHandlePx + 2;
            painter->setBrush(QColor(0, 80, 200, 200));
            painter->setPen(QPen(Qt::white, 1.5));
            painter->drawRect(px - hs, py - hs, hs * 2, hs * 2);
        }
    }

    // ── Snap indicator ────────────────────────────────────────────────────
    // Drawn on top of all handles so it's always visible. A green ring +
    // crosshair at the active snap candidate signals to the user that the
    // handle will lock to this point on release.
    if (m_snapping && m_editDragging) {
        int px = 0, py = 0;
        toPixelCoords(m_snapPt.x(), m_snapPt.y(), px, py);
        constexpr int sr = 10; // snap ring radius (px)
        painter->setPen(QPen(QColor(0, 210, 60), 2.0));
        painter->setBrush(QColor(0, 210, 60, 50));
        painter->drawEllipse(px - sr, py - sr, sr * 2, sr * 2);
        painter->setPen(QPen(QColor(0, 210, 60), 1.5));
        painter->drawLine(px - sr + 3, py, px + sr - 3, py);
        painter->drawLine(px, py - sr + 3, px, py + sr - 3);
    }

    painter->restore();
}

// ---------------------------------------------------------------------------
// Private helpers
// ---------------------------------------------------------------------------

void OpenSWMMVisMapToolSelect::selectAtPoint(const QPoint &pixel,
                                          Qt::KeyboardModifiers mods)
{
    if (!m_canvas)
        return;

    double mapX, mapY;
    toMapCoords(pixel.x(), pixel.y(), mapX, mapY);

    // Effective pixel tolerance = user preference floored at the
    // largest rendered marker's half-bound, plus a small 4 px halo.
    // This guarantees any click inside a visible glyph picks the
    // object regardless of the pixelTol setting — matching the user's
    // expectation "tolerance at least the size of the render bounds".
    // The floor is recomputed per invocation because symbology can
    // change at runtime (Preferences / Properties dialog).
    double markerFloorPx = 0.0;
    for (OpenSWMMVisLayer *l : m_canvas->layers()) {
        if (auto *sl = qobject_cast<SWMMModelLayer *>(l)) {
            if (l->isVisible())
                markerFloorPx = std::max(markerFloorPx,
                                          sl->maxMarkerHalfBoundPx());
        }
    }
    const double effectivePx = std::max(double(m_pixelTol),
                                         markerFloorPx + 4.0);

    double mapX2, mapY2;
    toMapCoords(pixel.x() + effectivePx, pixel.y() + effectivePx,
                mapX2, mapY2);
    const double tolX = std::abs(mapX2 - mapX);
    const double tolY = std::abs(mapY2 - mapY);
    const double tol  = std::max(tolX, tolY);

    for (OpenSWMMVisLayer *l : m_canvas->layers())
    {
        if (!l->isVisible())
            continue;

        if (auto *sl = qobject_cast<SWMMModelLayer *>(l))
        {
            // SWMM network click-pick. identifyAt returns the nearest
            // node / link / subcatchment / gage within the tolerance.
            // Note the lower-case keys "elementName" / "elementType"
            // — identifyByName uses capitalised "Name" / "Type"; the
            // two methods are historically inconsistent, and selection
            // has to key on the layer's SoA name exactly.
            const QVariantMap hit = sl->identifyAt(mapX, mapY, nullptr, tol);
            const QString name = hit.value(QStringLiteral("elementName")).toString();
            if (name.isEmpty()) continue;
            // Typed pick: the hit knows WHICH kind was clicked, and SWMM
            // names are per-type namespaces (a gage and a subcatchment may
            // share a name) — carrying the kind keeps a subcatchment click
            // from also selecting its same-named gage.
            const quint8 kind = kindBitForElementType(
                hit.value(QStringLiteral("elementType")).toString());

            QVector<SWMMModelLayer::SelectedElement> sel = sl->selectedElements();
            if (mods & Qt::ShiftModifier) {
                bool merged = false;
                for (auto &e : sel)
                    if (e.name == name) { e.kinds |= kind; merged = true; break; }
                if (!merged) sel.append({name, kind});
            } else if (mods & Qt::ControlModifier) {
                for (int i = sel.size() - 1; i >= 0; --i) {
                    if (sel[i].name != name) continue;
                    sel[i].kinds &= ~kind;
                    if (sel[i].kinds == 0) sel.remove(i);
                }
            } else {
                sel = { {name, kind} };
            }
            sl->setSelectedElements(sel);
            emit selectionChanged(sl);
            return;   // one click selects one object across all layers
        }

        if (auto *vl = qobject_cast<GISVectorLayer *>(l))
        {
            // Identify nearest feature
            QList<QVariantMap> features = vl->identifyAt(mapX, mapY, tol);
            if (features.isEmpty())
                continue;

            long long fid = features.first().value(QStringLiteral("fid"), -1LL).toLongLong();
            QSet<long long> ids = vl->selectedFeatureIds();

            if (mods & Qt::ShiftModifier)
                ids.insert(fid);
            else if (mods & Qt::ControlModifier)
                ids.remove(fid);
            else
            {
                ids.clear();
                ids.insert(fid);
            }

            vl->setSelectedFeatureIds(ids);
            emit selectionChanged(vl);
        }
    }

    // Clicked on empty space with no modifier → clear selections across
    // all SWMM layers so the Attribute Panel empties out. Ignore when
    // Shift/Ctrl is held so partial selections don't vanish on a miss.
    if (!(mods & (Qt::ShiftModifier | Qt::ControlModifier)))
    {
        for (OpenSWMMVisLayer *l : m_canvas->layers()) {
            if (auto *sl = qobject_cast<SWMMModelLayer *>(l)) {
                if (!sl->selectedElementNames().isEmpty()) {
                    sl->clearSelection();
                    emit selectionChanged(sl);
                }
            }
        }
    }
}

void OpenSWMMVisMapToolSelect::selectInRect(const QRect &pixelRect,
                                         Qt::KeyboardModifiers mods)
{
    if (!m_canvas)
        return;

    // Small-rubber-band → treat as a point click at the rect centre.
    // Without this, a tiny drag (jitter that crossed the threshold
    // but stayed < ~2× threshold) would hand the selection off to
    // the rect-select path, which selects EVERY feature in the rect
    // — nodes + their incident links together. Users expect
    // small-drag-near-a-node to behave like a click on the node.
    const int smallRectLimit = 2 * m_dragThreshPx;
    if (pixelRect.width() <= smallRectLimit
     && pixelRect.height() <= smallRectLimit) {
        selectAtPoint(pixelRect.center(), mods);
        return;
    }

    double x1, y1, x2, y2;
    toMapCoords(pixelRect.left(),  pixelRect.top(),    x1, y1);
    toMapCoords(pixelRect.right(), pixelRect.bottom(), x2, y2);

    const double minX = std::min(x1, x2), maxX = std::max(x1, x2);
    const double minY = std::min(y1, y2), maxY = std::max(y1, y2);
    MapExtent selection(minX, minY, maxX, maxY);

    for (OpenSWMMVisLayer *l : m_canvas->layers())
    {
        if (!l->isVisible())
            continue;

        if (auto *sl = qobject_cast<SWMMModelLayer *>(l))
        {
            // SWMM rubber-band select. All four kinds use accelerated
            // layer-side rect queries so a big-model rubber-band stays
            // interactive:
            //   - Nodes / Gages: nanoflann KD-tree → O(log N + k).
            //   - Links / Subcatchments: cached per-feature bboxes →
            //     O(N) with constant work per item.
            //   The layer methods also apply the inverse CRS transform
            //   internally so canvas-CRS click coords match against
            //   layer-CRS feature positions correctly.
            // The queries are per-kind, and the selection stays typed —
            // SWMM names are per-type namespaces, so a rect over a
            // subcatchment must not also select its same-named gage
            // unless the gage point itself is inside the rect.
            QElapsedTimer t; t.start();
            const auto nh = sl->nodesInRect(minX, minY, maxX, maxY);
            const qint64 t_n = t.elapsed();
            const auto gh = sl->gagesInRect(minX, minY, maxX, maxY);
            const qint64 t_g = t.elapsed() - t_n;
            const auto lh = sl->linksInRect(minX, minY, maxX, maxY);
            const qint64 t_l = t.elapsed() - t_n - t_g;
            const auto sh = sl->subcatchmentsInRect(minX, minY, maxX, maxY);
            const qint64 t_s = t.elapsed() - t_n - t_g - t_l;
            qDebug().noquote() << "[selectInRect] nodes=" << nh.size() << "(" << t_n << "ms)"
                               << " gages=" << gh.size() << "(" << t_g << "ms)"
                               << " links=" << lh.size() << "(" << t_l << "ms)"
                               << " subc="  << sh.size() << "(" << t_s << "ms)";

            QVector<SWMMModelLayer::SelectedElement> sel;
            if (mods & Qt::ControlModifier) {
                // Ctrl-rubber-band removes hit kinds from the existing
                // selection (entries emptied of every kind drop out).
                QHash<QString, quint8> hitBits;
                const auto collect = [&hitBits](const QStringList &names, quint8 kind) {
                    for (const QString &n : names) hitBits[n] |= kind;
                };
                collect(nh, SWMMModelLayer::kKindNode);
                collect(gh, SWMMModelLayer::kKindGage);
                collect(lh, SWMMModelLayer::kKindLink);
                collect(sh, SWMMModelLayer::kKindCatch);
                for (const auto &e : sl->selectedElements()) {
                    const quint8 kinds = e.kinds & ~hitBits.value(e.name, 0);
                    if (kinds) sel.append({e.name, kinds});
                }
            } else {
                if (mods & Qt::ShiftModifier)
                    sel = sl->selectedElements();
                QHash<QString, int> at;   // name → index in sel
                at.reserve(sel.size());
                for (int i = 0; i < sel.size(); ++i) at.insert(sel[i].name, i);
                const auto merge = [&sel, &at](const QStringList &names, quint8 kind) {
                    for (const QString &n : names) {
                        const auto it = at.constFind(n);
                        if (it != at.constEnd()) { sel[it.value()].kinds |= kind; continue; }
                        at.insert(n, sel.size());
                        sel.append({n, kind});
                    }
                };
                merge(nh, SWMMModelLayer::kKindNode);
                merge(gh, SWMMModelLayer::kKindGage);
                merge(lh, SWMMModelLayer::kKindLink);
                merge(sh, SWMMModelLayer::kKindCatch);
            }
            sl->setSelectedElements(sel);
            emit selectionChanged(sl);
            continue;
        }

        if (auto *vl = qobject_cast<GISVectorLayer *>(l))
        {
            // TODO: implement rect-based selection via GDAL spatial filter
            // For now use a simple extent-overlap check
            Q_UNUSED(selection)
            emit selectionChanged(vl);
        }
    }
}

void OpenSWMMVisMapToolSelect::showContextMenu(const QPoint &pixel)
{
    if (!m_canvas) return;

    double mapX, mapY;
    toMapCoords(pixel.x(), pixel.y(), mapX, mapY);
    double mapX2, mapY2;
    toMapCoords(pixel.x() + m_pixelTol, pixel.y() + m_pixelTol, mapX2, mapY2);
    const double tol = std::max(std::abs(mapX2 - mapX),
                                 std::abs(mapY2 - mapY));

    // Walk visible SWMM layers; first hit wins (matches left-click order).
    SWMMObjectRef ref{SWMMObjectRef::Unknown, {}};
    SWMMModelLayer *hitLayer = nullptr;
    for (OpenSWMMVisLayer *l : m_canvas->layers()) {
        if (!l->isVisible()) continue;
        auto *sl = qobject_cast<SWMMModelLayer *>(l);
        if (!sl) continue;
        const QVariantMap hit = sl->identifyAt(mapX, mapY, nullptr, tol);
        const QString name = hit.value(QStringLiteral("elementName")).toString();
        if (name.isEmpty()) continue;
        const QString type = hit.value(QStringLiteral("elementType")).toString();
        SWMMObjectRef::ObjectType t = SWMMObjectRef::Unknown;
        if      (type == QStringLiteral("Node"))         t = SWMMObjectRef::Node;
        else if (type == QStringLiteral("Link"))         t = SWMMObjectRef::Link;
        else if (type == QStringLiteral("Subcatchment")) t = SWMMObjectRef::Subcatchment;
        else if (type == QStringLiteral("RainGage"))     t = SWMMObjectRef::RainGage;
        ref = {t, name};
        hitLayer = sl;
        break;
    }
    auto *widget = qobject_cast<QWidget *>(m_canvas);
    const QPoint globalPt = widget ? widget->mapToGlobal(pixel) : pixel;

    // Slice AT.2 — background right-click opens a "Plot System Variable…"
    // submenu instead of doing nothing. Returns early so the rest of the
    // hit-object menu logic is untouched.
    if (ref.objectType == SWMMObjectRef::Unknown || ref.name.isEmpty())
    {
        QMenu bgMenu(widget);
        QMenu *sysMenu = openswmmvis::ui::AttributePickerMenu::createForSystem(
            openswmmvis::plot::UnitSystem::US, &bgMenu);
        if (sysMenu) {
            sysMenu->setTitle(QObject::tr("Plot System Variable…"));
            sysMenu->setIcon(QIcon(QStringLiteral(":/swmmvis/Chart")));
            bgMenu.addMenu(sysMenu);
            QAction *picked = bgMenu.exec(globalPt);
            const auto attr = openswmmvis::ui::AttributePickerMenu::attributeFrom(picked);
            if (attr != openswmmvis::plot::PlotAttribute::Unknown)
                emit plotSystemRequested(attr);
        }
        return;
    }

    // Right-click does NOT mutate the selection — users want it to act
    // on whatever's under the cursor without disturbing what's already
    // highlighted. Zoom-to-Object / Plot Time Series take the hit
    // ref directly without going through `setSelectedElementNames`.

    QMenu menu(widget);
    QAction *actZoom = menu.addAction(QIcon(QStringLiteral(":/swmmvis/Extent")),
                                      QObject::tr("Zoom to Object"));

    // Slice AT.2 — submenu of attributes valid for this object kind
    // (Node/Link/Subcatchment). RainGage still has no plot entry.
    //
    // Results-first selection: when more than one SWMM Output (.out)
    // layer is loaded, the entry becomes a two-level submenu
    // "Plot Time Series ▸ <layer> ▸ <variable>" so the user can pick
    // which results to plot against before picking the variable. With
    // 0/1 layers we keep the original flat submenu so single-output
    // workflows aren't slowed down by an extra hop.
    using PlotKind = openswmmvis::plot::ObjectRef::Kind;
    PlotKind plotKind = PlotKind::Unknown;
    switch (ref.objectType) {
    case SWMMObjectRef::Node:         plotKind = PlotKind::Node;     break;
    case SWMMObjectRef::Link:         plotKind = PlotKind::Link;     break;
    case SWMMObjectRef::Subcatchment: plotKind = PlotKind::Subcatch; break;
    default: break;
    }

    QList<SWMMResultsLayer *> resultsLayers;
    if (m_canvas) {
        for (OpenSWMMVisLayer *l : m_canvas->layers()) {
            if (auto *r = qobject_cast<SWMMResultsLayer *>(l))
                resultsLayers.push_back(r);
        }
    }

    QMenu *plotSubmenu = nullptr;                                 // flat case
    QList<QMenu *> perLayerSubmenus;                              // 2+ case
    QHash<QMenu *, SWMMResultsLayer *> submenuToLayer;            // 2+ case
    if (plotKind != PlotKind::Unknown) {
        if (resultsLayers.size() <= 1) {
            plotSubmenu = openswmmvis::ui::AttributePickerMenu::createForObjectKind(
                plotKind, openswmmvis::plot::UnitSystem::US, &menu);
            if (plotSubmenu) {
                plotSubmenu->setTitle(QObject::tr("Plot Time Series…"));
                plotSubmenu->setIcon(QIcon(QStringLiteral(":/swmmvis/Chart")));
                menu.addMenu(plotSubmenu);
            }
        } else {
            QMenu *topPlot = menu.addMenu(QIcon(QStringLiteral(":/swmmvis/Chart")),
                                           QObject::tr("Plot Time Series"));
            for (SWMMResultsLayer *r : resultsLayers) {
                QMenu *attrMenu = openswmmvis::ui::AttributePickerMenu::createForObjectKind(
                    plotKind, openswmmvis::plot::UnitSystem::US, topPlot);
                if (!attrMenu) continue;
                attrMenu->setTitle(r->name());
                topPlot->addMenu(attrMenu);
                perLayerSubmenus.push_back(attrMenu);
                submenuToLayer.insert(attrMenu, r);
            }
        }
    }

    menu.addSeparator();

    // Convert To ▸ — nodes and links only. The identify hit map carries
    // only elementType/elementName, so read the current kind live from the
    // engine (same pattern as the Attribute Table's Change Type). Using
    // ref.objectType to pick the node-vs-link namespace is deliberate: a
    // node and a link may share a name, so findObjectLocation would be
    // ambiguous here.
    QHash<QAction *, int> convertTargets;
    int currentType = -1;
    if (hitLayer && hitLayer->engine() &&
        (ref.objectType == SWMMObjectRef::Node ||
         ref.objectType == SWMMObjectRef::Link)) {
        SWMM_Engine eng = hitLayer->engine();
        const bool isNode = (ref.objectType == SWMMObjectRef::Node);
        const QByteArray id = ref.name.toUtf8();
        const int idx = isNode ? swmm_node_index(eng, id.constData())
                               : swmm_link_index(eng, id.constData());
        const int rcT = (idx < 0) ? -1
            : (isNode ? swmm_node_get_type(eng, idx, &currentType)
                      : swmm_link_get_type(eng, idx, &currentType));
        if (rcT == SWMM_OK) {
            // Virtual junctions surface as their own node kind: a flagged
            // node reports kVirtualNodeType so "Junction" (demote) becomes
            // an offered target, and the "Virtual Junction" target is
            // greyed out with the violated rule when the engine's usage
            // rules (two identical conduits, no inflows, …) aren't met.
            constexpr int kVJ = openswmmvis::ui::TypeConversionFlow::kVirtualNodeType;
            int vjRule = 0;
            if (isNode) {
                int isVirtual = 0;
                swmm_node_is_virtual(eng, idx, &isVirtual);
                if (isVirtual) currentType = kVJ;
                swmm_node_virtual_eligible(eng, idx, &vjRule);
            }
            QMenu *convertMenu = menu.addMenu(QObject::tr("Convert To"));
            convertMenu->setToolTipsVisible(true);
            const int nKinds = 5;
            for (int t = 0; t < nKinds; ++t) {
                if (t == currentType) continue;
                const QString label = isNode
                    ? openswmmvis::ui::TypeConversionFlow::nodeTypeLabel(t)
                    : openswmmvis::ui::TypeConversionFlow::linkTypeLabel(t);
                QAction *act = convertMenu->addAction(label);
                if (isNode && t == kVJ && vjRule != 0) {
                    act->setEnabled(false);
                    act->setToolTip(SWMMModelLayer::virtualJunctionRuleText(vjRule));
                }
                convertTargets.insert(act, t);
            }
        }
    }

    QAction *actDelete = menu.addAction(QObject::tr("Delete…"));

    QAction *picked = menu.exec(globalPt);
    if (!picked) return;

    // Convert To ▸ dispatch — run the shared confirm/convert/summary flow.
    // On success drop any now-stale inline vertex-edit session on this
    // element (e.g. conduit handles after a convert to pump).
    if (convertTargets.contains(picked) && hitLayer) {
        const bool isNode = (ref.objectType == SWMMObjectRef::Node);
        if (openswmmvis::ui::TypeConversionFlow::run(
                widget, hitLayer, isNode, ref.name,
                currentType, convertTargets.value(picked))) {
            if (m_editKind != EditKind::None && m_editName == ref.name)
                clearEditMode();
        }
        return;
    }

    // Slice AT.2 — submenu actions: dispatch to plotAttributeRequested.
    // attributeFrom() returns Unknown for the "All attributes" sentinel;
    // we forward that verbatim so swmmvis.cpp can fan out the series.
    if (plotSubmenu && picked->parent() == plotSubmenu) {
        const auto attr = openswmmvis::ui::AttributePickerMenu::attributeFrom(picked);
        emit plotAttributeRequested(ref, attr);
        return;
    }
    // Two-level layout: the picked QAction's parent is the per-layer
    // attribute submenu; we look that up to recover which results layer
    // the user chose.
    if (!perLayerSubmenus.isEmpty()) {
        auto *parentMenu = qobject_cast<QMenu *>(picked->parent());
        if (parentMenu && submenuToLayer.contains(parentMenu)) {
            const auto attr = openswmmvis::ui::AttributePickerMenu::attributeFrom(picked);
            emit plotAttributeForLayerRequested(ref, attr, submenuToLayer.value(parentMenu));
            return;
        }
    }

    if (picked == actZoom && hitLayer)
    {
        // If the right-clicked object is part of the current selection,
        // zoom to the combined canvas-CRS extent of all selected objects;
        // otherwise zoom to just the object under the cursor.
        const QStringList sel = hitLayer->selectedElementNames();
        const QStringList targets = (sel.size() > 1 && sel.contains(ref.name))
                                        ? sel
                                        : QStringList{ref.name};

        MapExtent combined;
        for (const QString &name : targets) {
            const MapExtent obj = m_canvas->extentInCanvasCRS(
                hitLayer, hitLayer->objectExtent(name));
            if (!std::isfinite(obj.xMin())) continue;
            combined = combined.isValid() ? combined.united(obj) : obj;
        }
        if (!combined.isValid()) return;

        double x0 = combined.xMin(), y0 = combined.yMin();
        double x1 = combined.xMax(), y1 = combined.yMax();
        const bool isPoint = (combined.width() == 0.0 && combined.height() == 0.0);
        if (isPoint) {
            double buffer = 100.0;
            if (const MapExtent le = m_canvas->layerExtentInCanvasCRS(hitLayer); le.isValid()) {
                const double dx = le.xMax() - le.xMin();
                const double dy = le.yMax() - le.yMin();
                buffer = std::max(25.0, 0.005 * std::max(dx, dy));
            }
            x0 -= buffer; y0 -= buffer;
            x1 += buffer; y1 += buffer;
        } else {
            const double padX = std::max(1e-6, combined.width()  * 0.25);
            const double padY = std::max(1e-6, combined.height() * 0.25);
            x0 -= padX; y0 -= padY;
            x1 += padX; y1 += padY;
        }
        MapExtent zoom(x0, y0, x1, y1);
        if (zoom.isValid()) m_canvas->setExtent(zoom);
    }
    else if (picked == actDelete && hitLayer)
    {
        // Select only the right-clicked object then delegate to the
        // shared delete handler (which confirms and builds the command).
        hitLayer->setSelectedElements(
            { {ref.name, kindBitForObjectType(ref.objectType)} });
        emit selectionChanged(hitLayer);
        deleteSelectedObjects();
    }
}

// ---------------------------------------------------------------------------
// Edit sub-mode — private helpers
// ---------------------------------------------------------------------------

void OpenSWMMVisMapToolSelect::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || !m_canvas) return;

    // Vertex editing requires an active edit session — without it, a
    // double-click is treated as a plain select with no edit side-effect.
    if (auto *pw = qobject_cast<SWMMVisProjectWindow *>(m_canvas->parent()))
        if (!pw->isEditSessionActive()) return;

    double mapX = 0.0, mapY = 0.0;
    toMapCoords(event->pos().x(), event->pos().y(), mapX, mapY);
    double mapX2 = 0.0, mapY2 = 0.0;
    toMapCoords(event->pos().x() + m_pixelTol, event->pos().y() + m_pixelTol,
                mapX2, mapY2);
    const double tol = std::max(std::abs(mapX2 - mapX), std::abs(mapY2 - mapY));

    for (OpenSWMMVisLayer *l : m_canvas->layers())
    {
        if (!l->isVisible()) continue;
        auto *sl = qobject_cast<SWMMModelLayer *>(l);
        if (!sl) continue;

        const auto r = sl->pickAt(mapX, mapY, tol);
        if (!r.valid) continue;

        // Double-clicking the already-active feature exits edit mode (toggle).
        if (m_editLayer == sl && m_editName == r.name)
        {
            clearEditMode();
            return;
        }

        if (r.cat == SWMMModelLayer::CatJunctions
         || r.cat == SWMMModelLayer::CatOutfalls
         || r.cat == SWMMModelLayer::CatStorage
         || r.cat == SWMMModelLayer::CatDividers)
        {
            enterEditMode(sl, r.name, EditKind::Node, r.soaIndex);
            return;
        }

        if (r.cat == SWMMModelLayer::CatConduits
         || r.cat == SWMMModelLayer::CatPumps
         || r.cat == SWMMModelLayer::CatOrifices
         || r.cat == SWMMModelLayer::CatWeirs
         || r.cat == SWMMModelLayer::CatOutlets)
        {
            enterEditMode(sl, r.name, EditKind::Link, r.soaIndex);
            return;
        }

        if (r.cat == SWMMModelLayer::CatSubcatchments)
        {
            enterEditMode(sl, r.name, EditKind::Subcatch, r.soaIndex);
            return;
        }
    }

    // Double-clicked empty space → exit edit mode.
    clearEditMode();
}

void OpenSWMMVisMapToolSelect::enterEditMode(SWMMModelLayer *layer,
                                              const QString  &name,
                                              EditKind        kind,
                                              int             soaIndex)
{
    clearEditMode();

    m_editLayer  = layer;
    m_editName   = name;
    m_editKind   = kind;
    m_editSoaIdx = soaIndex;

    if (kind == EditKind::Node)
    {
        double x = 0.0, y = 0.0;
        layer->cachedNodeCoord(soaIndex, &x, &y);
        m_editHandles   = { QPointF(x, y) };
        m_editNodeOrigX = x;
        m_editNodeOrigY = y;
    }
    else if (kind == EditKind::Link)
    {
        m_editHandles = layer->cachedLinkInteriorVertices(soaIndex);
    }
    else if (kind == EditKind::Subcatch)
    {
        m_editSubcatchVerts = layer->cachedSubcatchVertices(soaIndex);
        // Handle 0 = centroid (square), handles 1..N = polygon vertices (circles)
        double cx = 0.0, cy = 0.0;
        for (const QPointF &p : m_editSubcatchVerts) { cx += p.x(); cy += p.y(); }
        if (!m_editSubcatchVerts.isEmpty()) {
            cx /= m_editSubcatchVerts.size();
            cy /= m_editSubcatchVerts.size();
        }
        m_editHandles.clear();
        m_editHandles.reserve(1 + m_editSubcatchVerts.size());
        m_editHandles.append(QPointF(cx, cy));
        m_editHandles.append(m_editSubcatchVerts);
    }

    if (m_canvas)
        m_canvas->invalidate(MapCanvas::Overlay,
                             QStringLiteral("select-edit-enter"));
}

void OpenSWMMVisMapToolSelect::clearEditMode()
{
    // Roll back any live node-drag preview.
    if (m_editDragging && m_editKind == EditKind::Node
        && m_editLayer && m_editSoaIdx >= 0)
        m_editLayer->previewNodeMove(m_editSoaIdx, m_editNodeOrigX, m_editNodeOrigY);

    m_editKind             = EditKind::None;
    m_editLayer            = nullptr;
    m_editName.clear();
    m_editSoaIdx           = -1;
    m_editHandles.clear();
    m_editSubcatchVerts.clear();
    m_editSelectedHandles.clear();
    m_editDragging         = false;
    m_editDragHandle       = -1;
    m_editRubberbanding    = false;
    m_editPressedEmpty     = false;
    m_snapping             = false;

    if (m_canvas)
        m_canvas->invalidate(MapCanvas::Overlay | MapCanvas::Scene,
                             QStringLiteral("select-edit-clear"));
}

int OpenSWMMVisMapToolSelect::hitTestEditHandle(const QPoint &pixel) const
{
    for (int i = 0; i < m_editHandles.size(); ++i)
    {
        int hx = 0, hy = 0;
        toPixelCoords(m_editHandles[i].x(), m_editHandles[i].y(), hx, hy);
        const int dx = pixel.x() - hx;
        const int dy = pixel.y() - hy;
        if (dx * dx + dy * dy <= (kEditHandlePx + 3) * (kEditHandlePx + 3))
            return i;
    }
    return -1;
}

void OpenSWMMVisMapToolSelect::commitNodeDrag(double newX, double newY)
{
    if (!m_editLayer || m_editSoaIdx < 0 || !m_canvas) return;

    // Collect attached conduits and compute new lengths only when the
    // status-bar "Auto Length" toggle is on.
    const bool autoLength = m_canvas
        && m_canvas->property("autoLength").toBool();

    QVector<MoveNodeCommand::LengthRec> recs;
    if (autoLength)
    {
        const QVector<int> attached = m_editLayer->linksAttachedToNode(m_editSoaIdx);
        for (int linkIdx : attached)
        {
            if (!m_editLayer->isConduit(linkIdx)) continue;
            const double oldLen = m_editLayer->engineLinkLength(linkIdx);
            QVector<QPointF> poly = m_editLayer->cachedLinkPolyline(linkIdx);
            const int end = m_editLayer->linkEndForNode(linkIdx, m_editSoaIdx);
            if (end < 0 || poly.isEmpty()) continue;
            const int slot = (end == 0) ? 0 : (poly.size() - 1);
            poly = EditGeometry::replacedAt(poly, slot, QPointF(newX, newY));
            recs.append({linkIdx, oldLen,
                         m_editLayer->polylineLengthInModelUnits(poly)});
        }
    }

    // Restore cached state before pushing so MoveNodeCommand::undo has
    // the correct original coord to revert to.
    m_editLayer->previewNodeMove(m_editSoaIdx, m_editNodeOrigX, m_editNodeOrigY);

    auto *cmd = new MoveNodeCommand(m_editLayer, m_editSoaIdx,
                                    m_editNodeOrigX, m_editNodeOrigY,
                                    newX, newY, recs, m_canvas);
    if (m_canvas->undoStack())
        m_canvas->undoStack()->push(cmd);
    else
        delete cmd;

    // Update handle and snapshot so subsequent drags in the same session work.
    m_editHandles[0] = QPointF(newX, newY);
    m_editNodeOrigX  = newX;
    m_editNodeOrigY  = newY;

    m_canvas->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                         QStringLiteral("select-edit-node-commit"));
}

void OpenSWMMVisMapToolSelect::commitLinkDrag(QVector<QPointF> newInterior)
{
    if (!m_editLayer || m_editSoaIdx < 0 || !m_canvas) return;

    const QVector<QPointF> oldInterior =
        m_editLayer->cachedLinkInteriorVertices(m_editSoaIdx);

    // Recompute conduit length only when the status-bar "Auto Length" toggle is on.
    const bool autoLength = m_canvas
        && m_canvas->property("autoLength").toBool();

    bool applyLength = false;
    double oldLen = 0.0, newLen = 0.0;
    if (autoLength && m_editLayer->isConduit(m_editSoaIdx))
    {
        oldLen = m_editLayer->engineLinkLength(m_editSoaIdx);
        const QVector<QPointF> cached =
            m_editLayer->cachedLinkPolyline(m_editSoaIdx);
        QVector<QPointF> full;
        full.reserve(newInterior.size() + 2);
        if (!cached.isEmpty())  full.append(cached.first());
        full.append(newInterior);
        if (cached.size() >= 2) full.append(cached.last());
        newLen = m_editLayer->polylineLengthInModelUnits(full);
        applyLength = true;
    }

    auto *cmd = new EditVertexCommand(m_editLayer, m_editSoaIdx,
                                      oldInterior, newInterior,
                                      oldLen, newLen, applyLength, m_canvas);
    if (m_canvas->undoStack())
        m_canvas->undoStack()->push(cmd);
    else
        delete cmd;

    // Refresh tool-local copy so subsequent drags start from committed state.
    m_editHandles = newInterior;

    m_canvas->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                         QStringLiteral("select-edit-link-commit"));
}

void OpenSWMMVisMapToolSelect::selectHandlesInRect(const QRect &pixelRect)
{
    m_editSelectedHandles.clear();
    // For subcatch, start at 1 (skip centroid handle at 0).
    const int first = (m_editKind == EditKind::Subcatch) ? 1 : 0;
    for (int i = first; i < m_editHandles.size(); ++i)
    {
        int hx = 0, hy = 0;
        toPixelCoords(m_editHandles[i].x(), m_editHandles[i].y(), hx, hy);
        if (pixelRect.contains(QPoint(hx, hy)))
            m_editSelectedHandles.insert(i);
    }
    if (m_canvas)
        m_canvas->invalidate(MapCanvas::Overlay,
                             QStringLiteral("edit-select-handles"));
}

void OpenSWMMVisMapToolSelect::applyGroupDragDelta(double dx, double dy)
{
    if (m_editKind == EditKind::Link)
    {
        for (int i : std::as_const(m_editSelectedHandles))
        {
            if (i < 0 || i >= m_editHandles.size()) continue;
            m_editHandles[i] += QPointF(dx, dy);
        }
    }
    else if (m_editKind == EditKind::Subcatch)
    {
        for (int i : std::as_const(m_editSelectedHandles))
        {
            if (i <= 0 || i >= m_editHandles.size()) continue; // skip centroid
            m_editHandles[i] += QPointF(dx, dy);
            m_editSubcatchVerts[i - 1] += QPointF(dx, dy);
        }
        // Recompute centroid handle from the updated polygon.
        double cx = 0.0, cy = 0.0;
        for (const QPointF &p : m_editSubcatchVerts) { cx += p.x(); cy += p.y(); }
        cx /= m_editSubcatchVerts.size(); cy /= m_editSubcatchVerts.size();
        m_editHandles[0] = QPointF(cx, cy);
    }
}

void OpenSWMMVisMapToolSelect::commitSubcatchDrag(QVector<QPointF> newVertices)
{
    if (!m_editLayer || m_editSoaIdx < 0 || !m_canvas || newVertices.isEmpty())
        return;

    const QVector<QPointF> oldVertices =
        m_editLayer->cachedSubcatchVertices(m_editSoaIdx);

    // Compute old and new area when the status-bar auto-length toggle is on.
    const bool autoLength = m_canvas->property("autoLength").toBool();
    double oldArea = 0.0, newArea = 0.0;
    if (autoLength)
    {
        const bool isSI = UnitSystem::instance()->isSI();
        const double conv = isSI ? 10000.0 : 43560.0;  // m²→ha or ft²→ac
        // Old area from engine
        SWMM_Engine eng = m_editLayer->engine();
        if (eng) swmm_subcatch_get_area(eng, m_editSoaIdx, &oldArea);
        // New area from updated polygon geometry
        newArea = EditGeometry::polygonArea(newVertices) / conv;
    }

    auto *cmd = new EditSubcatchCommand(m_editLayer, m_editSoaIdx,
                                        oldVertices, newVertices,
                                        oldArea, newArea, autoLength,
                                        m_canvas);
    if (m_canvas->undoStack())
        m_canvas->undoStack()->push(cmd);
    else
        delete cmd;

    m_editSubcatchVerts = newVertices;

    m_canvas->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                         QStringLiteral("select-edit-subcatch-commit"));
}

void OpenSWMMVisMapToolSelect::deleteSelectedEditHandles()
{
    if (m_editSelectedHandles.isEmpty() || !m_editLayer || !m_canvas)
        return;

    if (m_editKind == EditKind::Link)
    {
        // Build new interior list by skipping every selected index.
        QVector<QPointF> newInterior;
        newInterior.reserve(m_editHandles.size());
        for (int i = 0; i < m_editHandles.size(); ++i)
        {
            if (!m_editSelectedHandles.contains(i))
                newInterior.append(m_editHandles[i]);
        }
        m_editSelectedHandles.clear();
        commitLinkDrag(newInterior);
    }
    else if (m_editKind == EditKind::Subcatch)
    {
        // Handle indices are 1-based (0 = centroid).  Collect 0-based vertex indices.
        QSet<int> vertexIndices;
        for (int h : std::as_const(m_editSelectedHandles))
            if (h > 0) vertexIndices.insert(h - 1);

        if (vertexIndices.isEmpty()) return;

        QVector<QPointF> newVerts;
        newVerts.reserve(m_editSubcatchVerts.size());
        for (int vi = 0; vi < m_editSubcatchVerts.size(); ++vi)
        {
            if (!vertexIndices.contains(vi))
                newVerts.append(m_editSubcatchVerts[vi]);
        }

        if (newVerts.size() >= 3)
        {
            m_editSelectedHandles.clear();
            commitSubcatchDrag(newVerts);
            // Rebuild handles from the new vertex list.
            double cx = 0.0, cy = 0.0;
            for (const QPointF &p : newVerts) { cx += p.x(); cy += p.y(); }
            cx /= newVerts.size(); cy /= newVerts.size();
            m_editHandles.clear();
            m_editHandles.reserve(1 + newVerts.size());
            m_editHandles.append(QPointF(cx, cy));
            m_editHandles.append(newVerts);
        }
        else
        {
            // Deletion would leave too few vertices — ask whether to delete the object.
            const int remaining = newVerts.size();
            const QString msg = tr(
                "Removing the %1 selected %2 would leave only %3 %4 — "
                "a subcatchment polygon requires at least 3.\n\n"
                "Delete the entire subcatchment \"%5\" instead?")
                .arg(vertexIndices.size())
                .arg(vertexIndices.size() == 1 ? tr("vertex") : tr("vertices"))
                .arg(remaining)
                .arg(remaining == 1 ? tr("vertex") : tr("vertices"))
                .arg(m_editName);

            const auto btn = QMessageBox::warning(
                m_canvas, tr("Cannot delete vertices"), msg,
                QMessageBox::Yes | QMessageBox::Cancel,
                QMessageBox::Cancel);

            if (btn != QMessageBox::Yes) return;

            // Delete the whole subcatchment.
            auto *cmd = new DeleteObjectCommand(
                m_editLayer, m_editName,
                DeleteObjectCommand::DeleteSubcatch, m_canvas);
            clearEditMode(); // exit edit mode before the object disappears
            if (m_canvas->undoStack())
                m_canvas->undoStack()->push(cmd);
            else
                delete cmd;
        }
    }
}
