/*!
 * \file   maptoolselect.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 */

#include "map/tools/maptoolselect.h"
#include "core/preferencesmanager.h"
#include "map/mapcanvas.h"
#include "map/mapextent.h"
#include "map/mapundostack.h"
#include "layers/gisvectorlayer.h"
#include "layers/swmmmodellayer.h"

#include <QAction>
#include <QDebug>
#include <QElapsedTimer>
#include <QIcon>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QVariantMap>
#include <QWidget>

#include <openswmm/engine/openswmm_links.h>
#include <openswmm/engine/openswmm_engine.h>

#include <algorithm>

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
    OpenSWMMVisMapTool::deactivate();
}

void OpenSWMMVisMapToolSelect::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton)
    {
        m_dragging     = false;
        m_startPixel   = event->pos();
        m_currentPixel = event->pos();
    }
    else if (event->button() == Qt::RightButton)
    {
        // Right-click: show the Zoom / Plot context menu for whatever
        // object sits under the cursor. If no object is hit this
        // silently does nothing — the empty menu would just annoy.
        showContextMenu(event->pos());
        event->accept();
    }
}

void OpenSWMMVisMapToolSelect::mouseMoveEvent(QMouseEvent *event)
{
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

    // Hover cursor feedback (Slice R Phase 4). While no button is held,
    // pick at the cursor and switch to a pointing-hand cursor if a
    // SWMM object is under the pointer, arrow otherwise. Gives users a
    // reliable "is this clickable?" cue without having to click to
    // find out.
    if (!m_canvas) return;
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

    // Selection-changed → repopulate scene items so their new highlight state
    // is drawn; also clear the rubber-band overlay. No raster reload needed.
    if (m_canvas)
        m_canvas->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                             QStringLiteral("select-tool-commit"));
}

void OpenSWMMVisMapToolSelect::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace)
    {
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

    const QStringList selected = sl->selectedElementNames();
    if (selected.isEmpty()) return;

    // Classify each selected object.
    struct ObjInfo { QString name; DeleteObjectCommand::TargetKind kind; };
    QList<ObjInfo> toDelete;
    QSet<QString> nodeNames;   // names of selected nodes
    QSet<QString> skipLinks;   // link names that will cascade-delete

    // First pass: identify nodes and their cascade links.
    for (const QString &name : selected) {
        SWMMModelLayer::Category cat;
        int soaIdx = -1;
        if (!sl->findObjectLocation(name, &cat, &soaIdx)) continue;

        if (cat == SWMMModelLayer::CatJunctions  ||
            cat == SWMMModelLayer::CatOutfalls    ||
            cat == SWMMModelLayer::CatStorage     ||
            cat == SWMMModelLayer::CatDividers)
        {
            nodeNames.insert(name);
            // Find cascade links.
            SWMM_Engine eng = sl->engine();
            const int ni = sl->nodeIndex(name);
            if (ni >= 0) {
                const int nLinks = swmm_link_count(eng);
                for (int li = 0; li < nLinks; ++li) {
                    int n1 = -1, n2 = -1;
                    swmm_link_get_from_node(eng, li, &n1);
                    swmm_link_get_to_node(eng, li, &n2);
                    if (n1 == ni || n2 == ni) {
                        const char *lid = swmm_link_id(eng, li);
                        if (lid) skipLinks.insert(QString::fromUtf8(lid));
                    }
                }
            }
        }
    }

    // Second pass: build the delete list, excluding cascade-handled links.
    for (const QString &name : selected) {
        SWMMModelLayer::Category cat;
        int soaIdx = -1;
        if (!sl->findObjectLocation(name, &cat, &soaIdx)) continue;

        DeleteObjectCommand::TargetKind kind;
        if (cat == SWMMModelLayer::CatJunctions  ||
            cat == SWMMModelLayer::CatOutfalls    ||
            cat == SWMMModelLayer::CatStorage     ||
            cat == SWMMModelLayer::CatDividers)
        {
            kind = DeleteObjectCommand::DeleteNode;
        } else if (cat == SWMMModelLayer::CatConduits ||
                   cat == SWMMModelLayer::CatPumps    ||
                   cat == SWMMModelLayer::CatOrifices ||
                   cat == SWMMModelLayer::CatWeirs    ||
                   cat == SWMMModelLayer::CatOutlets)
        {
            if (skipLinks.contains(name)) continue; // handled by node cascade
            kind = DeleteObjectCommand::DeleteLink;
        } else if (cat == SWMMModelLayer::CatRainGages) {
            kind = DeleteObjectCommand::DeleteGage;
        } else if (cat == SWMMModelLayer::CatSubcatchments) {
            kind = DeleteObjectCommand::DeleteSubcatch;
        } else {
            continue;
        }
        toDelete.append({name, kind});
    }

    if (toDelete.isEmpty()) return;

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

    // Group all deletes under one parent so Ctrl+Z undoes them together.
    auto *macro = new QUndoCommand(QObject::tr("Delete Objects"));
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
    if (!m_dragging)
        return;

    QRect rect = QRect(m_startPixel, m_currentPixel).normalized();
    painter->save();
    painter->setPen(QPen(m_rubberColor.darker(130), 1));
    painter->setBrush(m_rubberColor);
    painter->drawRect(rect);
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

            QStringList names = sl->selectedElementNames();
            if (mods & Qt::ShiftModifier) {
                if (!names.contains(name)) names << name;
            } else if (mods & Qt::ControlModifier) {
                names.removeAll(name);
            } else {
                names.clear();
                names << name;
            }
            sl->setSelectedElementNames(names);
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
            // SWMM rubber-band select: walk the SoA, accept anything
            // whose representative point/bbox falls in the rect. This
            // is the naive O(N) path; the Slice R spatial-index follow-up
            // will route it through queryInRect for millions-of-points
            // scaling.
            QSet<QString> hits;
            for (const auto &n : sl->selectedElementNames()) hits.insert(n);

            // Reset in replace-mode; otherwise fold into the existing set.
            if (!(mods & (Qt::ShiftModifier | Qt::ControlModifier)))
                hits.clear();

            // Walk each category row-wise; O(N) across all kinds, which
            // is the naive path until the Slice R spatial-index
            // follow-up lands a queryInRect on the layer. `isHit` is
            // the per-feature predicate (point-in-rect for nodes/gages,
            // bbox-overlap for links/subcatchments).
            auto sweepCategory = [&](SWMMModelLayer::Category c,
                                     auto isHit) {
                const int rows = sl->categoryCount(c);
                for (int r = 0; r < rows; ++r) {
                    const QString name = sl->objectNameAt(c, r);
                    if (name.isEmpty()) continue;
                    if (isHit(name))
                        hits.insert(name);
                }
            };

            // All four kinds use accelerated layer-side rect queries so
            // a big-model rubber-band stays interactive:
            //   - Nodes / Gages: nanoflann KD-tree → O(log N + k).
            //   - Links / Subcatchments: cached per-feature bboxes →
            //     O(N) with constant work per item, replacing the
            //     previous O(N²) name → linkIndex linear-scan + per-
            //     iteration vertex bbox compute.
            //   The layer methods also apply the inverse CRS transform
            //   internally so canvas-CRS click coords match against
            //   layer-CRS feature positions correctly.
            QElapsedTimer t; t.start();
            const auto nh = sl->nodesInRect(minX, minY, maxX, maxY);
            const qint64 t_n = t.elapsed();
            const auto gh = sl->gagesInRect(minX, minY, maxX, maxY);
            const qint64 t_g = t.elapsed() - t_n;
            const auto lh = sl->linksInRect(minX, minY, maxX, maxY);
            const qint64 t_l = t.elapsed() - t_n - t_g;
            const auto sh = sl->subcatchmentsInRect(minX, minY, maxX, maxY);
            const qint64 t_s = t.elapsed() - t_n - t_g - t_l;
            for (const QString &name : nh) hits.insert(name);
            for (const QString &name : gh) hits.insert(name);
            for (const QString &name : lh) hits.insert(name);
            for (const QString &name : sh) hits.insert(name);
            qDebug().noquote() << "[selectInRect] nodes=" << nh.size() << "(" << t_n << "ms)"
                               << " gages=" << gh.size() << "(" << t_g << "ms)"
                               << " links=" << lh.size() << "(" << t_l << "ms)"
                               << " subc="  << sh.size() << "(" << t_s << "ms)"
                               << " hits_total=" << hits.size();
            Q_UNUSED(sweepCategory);   // retained above for any future per-category fallback

            // Ctrl-rubber-band removes hits from the existing selection.
            QStringList result;
            if (mods & Qt::ControlModifier) {
                for (const auto &n : sl->selectedElementNames())
                    if (!hits.contains(n)) result << n;
            } else {
                result = QStringList(hits.cbegin(), hits.cend());
            }
            sl->setSelectedElementNames(result);
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
    if (ref.objectType == SWMMObjectRef::Unknown || ref.name.isEmpty())
        return;

    // Right-click does NOT mutate the selection — users want it to act
    // on whatever's under the cursor without disturbing what's already
    // highlighted. Zoom-to-Object / Plot Time Series take the hit
    // ref directly without going through `setSelectedElementNames`.

    auto *widget = qobject_cast<QWidget *>(m_canvas);
    QMenu menu(widget);
    QAction *actZoom = menu.addAction(QIcon(QStringLiteral(":/swmmvis/Extent")),
                                      QObject::tr("Zoom to Object"));
    QAction *actPlot = nullptr;
    if (ref.objectType == SWMMObjectRef::Node
        || ref.objectType == SWMMObjectRef::Link
        || ref.objectType == SWMMObjectRef::Subcatchment)
    {
        actPlot = menu.addAction(QIcon(QStringLiteral(":/swmmvis/Chart")),
                                 QObject::tr("Plot Time Series…"));
    }
    menu.addSeparator();
    QAction *actDelete = menu.addAction(QObject::tr("Delete…"));

    const QPoint globalPt = widget ? widget->mapToGlobal(pixel) : pixel;
    QAction *picked = menu.exec(globalPt);
    if (!picked) return;

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
    else if (actPlot && picked == actPlot)
    {
        emit plotTimeSeriesRequested(ref);
    }
    else if (picked == actDelete && hitLayer)
    {
        // Select only the right-clicked object then delegate to the
        // shared delete handler (which confirms and builds the command).
        hitLayer->setSelectedElementNames({ref.name});
        emit selectionChanged(hitLayer);
        deleteSelectedObjects();
    }
}
