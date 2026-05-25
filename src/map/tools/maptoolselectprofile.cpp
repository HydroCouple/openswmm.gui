/*!
 * \file   maptoolselectprofile.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "map/tools/maptoolselectprofile.h"

#include "core/preferencesmanager.h"
#include "layers/openswmmvislayer.h"
#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/openswmmvisscene.h"
#include "map/mapextent.h"
#include "map/profilepathoverlay.h"
#include "plot/profilenetworkadapter.h"
#include "ui/dialogs/profilepathpickerdialog.h"

#include <limits>

#include <QApplication>
#include <QFuture>
#include <QFutureWatcher>
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QString>
#include <QtConcurrent/QtConcurrent>

namespace {

constexpr int kTargetClickRadiusPx = 12;

// True when the mouse event carries the "second endpoint" modifier
// (Ctrl on Windows/Linux, Cmd on macOS).  Qt maps both to ControlModifier
// in the platform-aware modifier set, so we just check that.
bool hasEndpointModifier(QMouseEvent *event)
{
    return event->modifiers().testFlag(Qt::ControlModifier);
}

bool hasWaypointModifier(QMouseEvent *event)
{
    return event->modifiers().testFlag(Qt::ShiftModifier);
}

} // namespace

OpenSWMMVisMapToolSelectProfile::OpenSWMMVisMapToolSelectProfile(
    MapCanvas *canvas, QObject *parent)
    : OpenSWMMVisMapTool(QStringLiteral("SelectProfile"), canvas, parent)
{
}

OpenSWMMVisMapToolSelectProfile::~OpenSWMMVisMapToolSelectProfile()
{
    // m_overlay is a QGraphicsItem owned by the scene we added it to.
    // During project-window teardown, child destruction order is not
    // guaranteed; the QGraphicsScene (a member of MapCanvas, itself a
    // sibling QObject child) may have already destructed and freed the
    // overlay before we reach this destructor.  Touch the overlay only
    // when its tracked scene is still alive — otherwise the storage is
    // gone and `m_overlay->scene()` is a use-after-free.
    if (m_overlay && m_overlayScene) {
        m_overlayScene->removeItem(m_overlay);
        delete m_overlay;
    }
    m_overlay = nullptr;
}

void OpenSWMMVisMapToolSelectProfile::activate()
{
    OpenSWMMVisMapTool::activate();
    // Clear any in-progress capture but leave the previously-accepted
    // profile + its map overlay visible — so toggling the tool back on
    // shows whatever the user last selected instead of starting fresh.
    clearInProgress();

    // Seed endpoints from the active SWMMModelLayer's current selection so
    // the user can pre-select two nodes with the Select tool and then click
    // the profile button to route immediately.  Selection order is the
    // QStringList order returned by selectedElementNames(), which mirrors
    // shift-click order (first click first).
    SWMMModelLayer *model = activeModel();
    QVector<int> seededNodes;
    if (model) {
        const QStringList names = model->selectedElementNames();
        for (const QString &n : names) {
            SWMMModelLayer::Category cat;
            int row = -1;
            if (!model->findObjectLocation(n, &cat, &row)) continue;
            if (cat != SWMMModelLayer::CatJunctions
             && cat != SWMMModelLayer::CatOutfalls
             && cat != SWMMModelLayer::CatStorage
             && cat != SWMMModelLayer::CatDividers) continue;
            const int engIdx = ProfileNetworkAdapter::findNodeIndex(model, n);
            if (engIdx < 0) continue;
            if (seededNodes.contains(engIdx)) continue;  // dedupe
            seededNodes.push_back(engIdx);
            if (seededNodes.size() == 2) break;
        }
    }

    auto ensureOverlay = [this, model]() {
        if (m_overlay) return;
        m_overlay = new ProfilePathOverlay(model);
        if (canvas() && canvas()->mapScene()) {
            canvas()->mapScene()->addItem(m_overlay);
            m_overlayScene = canvas()->mapScene();
        }
    };

    if (seededNodes.size() >= 2) {
        // Two pre-selected nodes → route immediately on activation.  Any
        // previously-accepted path is replaced by the new routing result
        // (or restored if routing produces nothing).
        m_acceptedPath = ProfileRouter::Path{};
        m_startNode    = seededNodes[0];
        m_endNode      = seededNodes[1];
        ensureOverlay();
        m_overlay->setPaths({});
        m_overlay->setEndpoints(m_startNode, m_endNode);
        if (canvas())
            canvas()->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                                 QStringLiteral("profile endpoints seeded"));
        m_state = State::PickingPath;
        emit statusMessageChanged(
            tr("Routing profile between the two selected nodes…"));
        routeAndPick();
        return;
    }

    if (seededNodes.size() == 1) {
        // Single pre-selected node → use as start, wait for end click.
        m_startNode = seededNodes[0];
        m_endNode   = -1;
        m_state     = State::AwaitingEnd;
        ensureOverlay();
        m_overlay->setPaths({});
        m_overlay->setEndpoints(m_startNode, -1);
        if (canvas())
            canvas()->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                                 QStringLiteral("profile start seeded"));
        emit statusMessageChanged(
            tr("Start node taken from current selection. "
               "Click the end node or link to route."));
        return;
    }

    if (!m_acceptedPath.linkIds.isEmpty()) {
        emit statusMessageChanged(
            tr("Showing selected profile (%1 links).  "
               "Click to trace a new one.")
                .arg(m_acceptedPath.linkIds.size()));
    } else {
        emit statusMessageChanged(
            tr("Click the profile start node or link, then Ctrl/⌘+click the end."));
    }
}

void OpenSWMMVisMapToolSelectProfile::deactivate()
{
    // Same idea on deactivate: preserve the accepted-path overlay so it
    // remains visible (or at least re-appears next activation).
    clearInProgress();
    OpenSWMMVisMapTool::deactivate();
}

ProfileRouter::Path OpenSWMMVisMapToolSelectProfile::acceptedPath() const
{
    return m_acceptedPath;
}

void OpenSWMMVisMapToolSelectProfile::clearSelection()
{
    const bool hadAnything =
        !m_acceptedPath.linkIds.isEmpty()
        || m_state != State::Idle
        || m_overlay
        || !m_waypoints.isEmpty();
    if (!hadAnything) return;
    resetState();
    if (canvas()) {
        canvas()->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                             QStringLiteral("profile session exited"));
    }
    emit statusMessageChanged(tr("Profile session ended."));
}

// ---------------------------------------------------------------------------
// Event handlers
// ---------------------------------------------------------------------------

void OpenSWMMVisMapToolSelectProfile::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton) {
        resetState();
        emit statusMessageChanged(tr("Profile selection cancelled."));
        return;
    }
    if (event->button() != Qt::LeftButton) return;

    SWMMModelLayer *model = activeModel();
    if (!model) {
        emit statusMessageChanged(tr("No model layer is active."));
        return;
    }

    double mx = 0.0, my = 0.0;
    toMapCoords(event->pos().x(), event->pos().y(), mx, my);
    const double tol = clickTolerance();
    const int hitNode = resolveNodeAt(mx, my, tol);
    if (hitNode < 0) {
        // Click on empty space (no node / link under cursor).  If a
        // profile is currently shown on the map, dismiss it — the user
        // is signalling they want to start over.  Otherwise nudge them
        // to snap closer.
        if (m_state == State::Idle && !m_acceptedPath.linkIds.isEmpty()) {
            resetState();
            if (canvas())
                canvas()->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                                     QStringLiteral("profile cleared"));
            emit statusMessageChanged(tr("Profile selection cleared."));
        } else {
            emit statusMessageChanged(
                tr("No node or link at click position — try snapping closer."));
        }
        return;
    }

    switch (m_state) {
    case State::Idle:
        // Starting a fresh selection — clear any persisted selected-path
        // highlight left from a previous successful selection.
        m_startNode = hitNode;
        m_endNode   = -1;
        m_acceptedPath = ProfileRouter::Path{};
        m_state     = State::AwaitingEnd;
        if (!m_overlay) {
            m_overlay = new ProfilePathOverlay(model);
            if (canvas() && canvas()->mapScene()) {
                canvas()->mapScene()->addItem(m_overlay);
                m_overlayScene = canvas()->mapScene();
            }
        }
        m_overlay->setPaths({});
        m_overlay->setEndpoints(m_startNode, /*endIdx=*/-1);
        if (canvas())
            canvas()->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                                 QStringLiteral("profile start captured"));
        emit statusMessageChanged(
            tr("Start node captured. Click the end node or link to route."));
        break;

    case State::AwaitingEnd:
        if (hasWaypointModifier(event)) {
            // Shift+click adds an intermediate waypoint (power-user shortcut).
            m_waypoints.push_back(hitNode);
            emit statusMessageChanged(
                tr("Waypoint added (%1 total). Click the end to route.")
                    .arg(m_waypoints.size()));
        } else {
            // Plain click → second endpoint; route.
            if (hitNode == m_startNode) {
                emit statusMessageChanged(
                    tr("Start and end nodes must differ. Click a different end."));
                return;
            }
            m_endNode = hitNode;
            m_overlay->setEndpoints(m_startNode, m_endNode);
            m_state = State::PickingPath;
            routeAndPick();
        }
        break;

    case State::PickingPath:
        // Picker dialog is modal so we shouldn't normally receive clicks
        // here.  In case it ever ends up behind another window (e.g. an
        // external modal interrupting the flow), raise it and surface a
        // status message so the user isn't left wondering why their click
        // "did nothing".
        if (m_pendingPicker) {
            m_pendingPicker->raise();
            m_pendingPicker->activateWindow();
        }
        emit statusMessageChanged(
            tr("Confirm or cancel the profile-path picker dialog before "
               "clicking another endpoint."));
        break;
    }
}

void OpenSWMMVisMapToolSelectProfile::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        resetState();
        emit statusMessageChanged(tr("Profile selection cancelled."));
    }
}

// ---------------------------------------------------------------------------
// Hit-testing
// ---------------------------------------------------------------------------

double OpenSWMMVisMapToolSelectProfile::clickTolerance() const
{
    if (!canvas() || canvas()->width() <= 0) return 1.0;

    // Effective pixel tolerance mirrors the Select tool's policy
    // (maptoolselect.cpp:887-896): the user-preference click radius,
    // floored by the largest rendered glyph's half-bound plus a 4 px
    // halo.  Without this floor the profile tool used a hardcoded 12 px
    // radius, which fell short of the 12.5 px outfall / 12 px storage
    // glyph radii on edge-of-glyph clicks — users reported "outfalls
    // can't be selected".  Including the marker bound guarantees any
    // click inside a visible glyph hits the node regardless of zoom.
    auto *prefs = PreferencesManager::instance();
    const int prefPx = prefs ? prefs->clickTolerancePx() : 16;

    double markerFloorPx = 0.0;
    if (canvas()) {
        for (OpenSWMMVisLayer *l : canvas()->layers()) {
            if (auto *sl = qobject_cast<SWMMModelLayer *>(l)) {
                if (l->isVisible())
                    markerFloorPx = std::max(markerFloorPx,
                                             sl->maxMarkerHalfBoundPx());
            }
        }
    }
    const double effectivePx = std::max(double(prefPx),
                                         markerFloorPx + 4.0);

    double mx1 = 0.0, my1 = 0.0;
    double mx2 = 0.0, my2 = 0.0;
    const_cast<OpenSWMMVisMapToolSelectProfile *>(this)
        ->toMapCoords(0, 0, mx1, my1);
    const_cast<OpenSWMMVisMapToolSelectProfile *>(this)
        ->toMapCoords(effectivePx, effectivePx, mx2, my2);
    return std::max(std::abs(mx2 - mx1), std::abs(my2 - my1));
}

int OpenSWMMVisMapToolSelectProfile::resolveNodeAt(double mapX,
                                                    double mapY,
                                                    double tolerance) const
{
    SWMMModelLayer *model = activeModel();
    if (!model) return -1;

    const auto hit = model->pickAt(mapX, mapY, tolerance);
    if (!hit.valid) return -1;

    // Node hit: resolve by name → engine node index.
    if (hit.cat == SWMMModelLayer::CatJunctions
        || hit.cat == SWMMModelLayer::CatOutfalls
        || hit.cat == SWMMModelLayer::CatStorage
        || hit.cat == SWMMModelLayer::CatDividers) {
        return ProfileNetworkAdapter::findNodeIndex(model, hit.name);
    }

    // Subcatchments / gages aren't routable endpoints.
    if (hit.cat == SWMMModelLayer::CatSubcatchments
        || hit.cat == SWMMModelLayer::CatRainGages) {
        return -1;
    }

    // Link hit: snap to the nearer endpoint of the link's polyline.
    const int linkIdx = ProfileNetworkAdapter::findLinkIndex(model, hit.name);
    if (linkIdx < 0) return -1;
    const int fromNode = model->linkFromNodeIdx(linkIdx);
    const int toNode   = model->linkToNodeIdx(linkIdx);
    if (fromNode < 0 && toNode < 0) return -1;
    double fx = 0.0, fy = 0.0, tx = 0.0, ty = 0.0;
    const bool fromOk = (fromNode >= 0) && model->cachedNodeCoord(fromNode, &fx, &fy);
    const bool toOk   = (toNode   >= 0) && model->cachedNodeCoord(toNode,   &tx, &ty);
    if (!fromOk && !toOk) return -1;
    if (!fromOk) return toNode;
    if (!toOk)   return fromNode;
    const double df2 = (fx - mapX) * (fx - mapX) + (fy - mapY) * (fy - mapY);
    const double dt2 = (tx - mapX) * (tx - mapX) + (ty - mapY) * (ty - mapY);
    return (df2 <= dt2) ? fromNode : toNode;
}

// ---------------------------------------------------------------------------
// Routing + picker
// ---------------------------------------------------------------------------

void OpenSWMMVisMapToolSelectProfile::routeAndPick()
{
    SWMMModelLayer *model = activeModel();
    if (!model || m_startNode < 0 || m_endNode < 0) {
        resetState();
        return;
    }

    // Build the routing graph on the main thread — it touches the engine
    // and model layer, which aren't reentrant.  The Graph is a POD so we
    // can safely pass it by value to the worker thread.
    ProfileRouter::Graph graph =
        ProfileNetworkAdapter::buildGraphFromModel(model);

    ProfileRouter::Options opts;
    // Undirected so users picking endpoints think in terms of "is this
    // node reachable" rather than "could flow physically travel that
    // way".  Pumps / weirs / orifices / outlets are traversable in
    // either direction for routing purposes.
    //
    // Using kShortestPaths (Yen's) — strictly Dijkstra-based so no DFS
    // backtracking is needed; each path is found in O(E log V) and we
    // run k of them sequentially.  Far cheaper than the exhaustive
    // enumerator on dense networks, and the result is already sorted
    // shortest-first.  k is user-tunable via Preferences.
    opts.undirected = true;
    opts.k          = PreferencesManager::instance()->profileMaxPaths();
    opts.softCapMs  = 0;     // no soft cap on background thread — UI stays responsive
    opts.maxIterations = std::numeric_limits<int>::max();

    // Snapshot routing inputs for the worker thread.  We do *not* capture
    // `this` inside the QtConcurrent lambda — only POD copies — so the
    // worker is safe even if the tool is destroyed mid-routing.
    const int startNode = m_startNode;
    const int endNode   = m_endNode;
    const QVector<int> waypoints = m_waypoints;

    // Endpoint halos and any in-progress overlay state should already be
    // visible from mousePressEvent before we got here, but force a
    // canvas invalidate one more time so the user sees the captured
    // endpoints render *before* the busy dialog appears.
    if (m_overlay)
        m_overlay->setEndpoints(m_startNode, m_endNode);
    if (canvas())
        canvas()->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                             QStringLiteral("profile endpoints captured"));
    QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);

    // Tear down any prior watcher (e.g. user clicked again before the
    // previous routing finished).  Cancelling a QtConcurrent::run future
    // does not actually stop the worker, so we just disconnect from it.
    if (m_routingWatcher) {
        m_routingWatcher->disconnect();
        m_routingWatcher->deleteLater();
        m_routingWatcher = nullptr;
        emit routingBusyChanged(false);
    }

    // Surface a busy state to the host (status-bar progress bar +
    // message).  No modal dialog — the map should remain fully
    // interactive while routing runs in the background.
    emit routingBusyChanged(true);
    emit statusMessageChanged(
        tr("Computing up to %1 profile path(s) between selected endpoints…")
            .arg(opts.k));

    // QFutureWatcher signals completion on the main thread (Qt::AutoConnection
    // posts to the GUI event loop when the worker thread finishes).
    auto *watcher = new QFutureWatcher<ProfileRouter::Result>(this);
    m_routingWatcher = watcher;
    const int graphNodes = graph.nodeCount;
    const int graphEdges = graph.edges.size();

    QObject::connect(watcher, &QFutureWatcher<ProfileRouter::Result>::finished,
                     this, [this, watcher, graphNodes, graphEdges]() {
        if (m_routingWatcher != watcher) {
            watcher->deleteLater();
            return;  // superseded by newer routing
        }
        const ProfileRouter::Result result = watcher->result();
        watcher->deleteLater();
        m_routingWatcher = nullptr;
        emit routingBusyChanged(false);
        onRoutingComplete(result, graphNodes, graphEdges);
    });

    // Kick off the worker thread.
    QFuture<ProfileRouter::Result> future;
    if (waypoints.isEmpty()) {
        future = QtConcurrent::run(
            [graph, startNode, endNode, opts]() {
                return ProfileRouter::kShortestPaths(
                    graph, startNode, endNode, opts);
            });
    } else {
        QVector<int> sequence;
        sequence.reserve(waypoints.size() + 2);
        sequence.push_back(startNode);
        for (int w : waypoints) sequence.push_back(w);
        sequence.push_back(endNode);
        future = QtConcurrent::run(
            [graph, sequence, opts]() {
                ProfileRouter::Options legOpts = opts;
                legOpts.k = 1;
                return ProfileRouter::kShortestPathsThrough(
                    graph, sequence, legOpts);
            });
    }
    watcher->setFuture(future);
}

void OpenSWMMVisMapToolSelectProfile::onRoutingComplete(
    const ProfileRouter::Result &result,
    int graphNodes, int graphEdges)
{
    SWMMModelLayer *model = activeModel();
    if (!model) {
        resetState();
        return;
    }

    if (!result.error.isEmpty()) {
        emit statusMessageChanged(
            tr("Profile routing error: %1 (start=node #%2, end=node #%3, "
               "graph nodes=%4 edges=%5)")
                .arg(result.error)
                .arg(m_startNode).arg(m_endNode)
                .arg(graphNodes).arg(graphEdges));
        resetState();
        return;
    }
    if (result.paths.isEmpty()) {
        emit statusMessageChanged(
            tr("No connected path between start node #%1 and end node #%2 "
               "(graph nodes=%3 edges=%4).")
                .arg(m_startNode).arg(m_endNode)
                .arg(graphNodes).arg(graphEdges));
        resetState();
        return;
    }
    emit statusMessageChanged(
        tr("Profile routing complete: %1 path(s) found%2.")
            .arg(result.paths.size())
            .arg(result.truncated
                 ? tr(" (truncated — raise max paths to see more)")
                 : QString()));

    if (m_overlay) m_overlay->setPaths(result.paths);
    if (canvas())
        canvas()->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                             QStringLiteral("profile candidates rendered"));

    // Reduces the overlay to a single highlighted candidate (the accepted
    // path) and keeps it visible on the map so the user can see what was
    // selected.  The tool returns to Idle so the next click starts a fresh
    // selection — clearing the overlay then.
    auto acceptPath = [this](const ProfileRouter::Path &p) {
        m_acceptedPath = p;
        if (m_overlay) {
            // Show only the accepted path on the overlay.
            QVector<ProfileRouter::Path> just{ p };
            m_overlay->setPaths(just);
            m_overlay->setHighlightedPath(0);
            m_overlay->setEndpoints(m_startNode, m_endNode);
        }
        if (canvas())
            canvas()->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                                 QStringLiteral("profile path accepted"));
        // Endpoints / waypoints cleared, but overlay persists.
        m_state = State::Idle;
        m_waypoints.clear();
        emit profilePathSelected(m_acceptedPath);
        emit statusMessageChanged(
            tr("Profile path selected (%1 links). "
               "Click again to trace another profile.")
                .arg(m_acceptedPath.linkIds.size()));
    };

    // Single path → accept immediately, no dialog.
    if (result.paths.size() == 1) {
        acceptPath(result.paths.first());
        return;
    }

    // Multiple → non-modal floating picker dialog, hover-synced to the
    // overlay.  Non-modal so the user can pan/zoom the map and inspect
    // candidate paths while the picker is open.  Qt::Tool + parenting to
    // the canvas's top-level widget makes the dialog a floating panel
    // (NSPanel on macOS) that stays above the main window without
    // stealing keyboard focus — much more reliable than
    // WindowStaysOnTopHint, which on macOS can let the dialog fall
    // behind the main NSWindow.  WindowStaysOnTopHint is added as a
    // secondary hint for X11 / Windows where Tool alone isn't enough.
    if (m_pendingPicker) {
        m_pendingPicker->close();
        m_pendingPicker = nullptr;
    }
    m_pendingPaths = result.paths;
    QWidget *parentTop = canvas() ? canvas()->window() : nullptr;
    auto *dlg = new ProfilePathPickerDialog(model, result.paths,
                                            /*truncated=*/result.truncated,
                                            /*parent=*/parentTop);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setModal(false);
    dlg->setWindowFlags(Qt::Tool | Qt::WindowStaysOnTopHint
                        | Qt::CustomizeWindowHint
                        | Qt::WindowTitleHint
                        | Qt::WindowCloseButtonHint);
    m_pendingPicker = dlg;

    QObject::connect(dlg, &ProfilePathPickerDialog::hoveredPathChanged,
                     this, [this](int idx) {
        if (m_overlay) m_overlay->setHighlightedPath(idx);
        if (canvas())
            canvas()->invalidate(MapCanvas::Scene | MapCanvas::Overlay,
                                 QStringLiteral("profile path hover"));
    });
    QObject::connect(dlg, &ProfilePathPickerDialog::zoomToPathRequested,
                     this, [this, model](int idx) {
        if (idx < 0 || idx >= m_pendingPaths.size()) return;
        if (!canvas() || !model) return;
        const auto &p = m_pendingPaths[idx];
        double xMin =  std::numeric_limits<double>::infinity();
        double yMin =  std::numeric_limits<double>::infinity();
        double xMax = -std::numeric_limits<double>::infinity();
        double yMax = -std::numeric_limits<double>::infinity();
        auto extend = [&](double x, double y) {
            xMin = std::min(xMin, x); yMin = std::min(yMin, y);
            xMax = std::max(xMax, x); yMax = std::max(yMax, y);
        };
        for (int n : p.nodes) {
            double x = 0, y = 0;
            if (model->cachedNodeCoord(n, &x, &y)) extend(x, y);
        }
        for (int e : p.linkIds) {
            for (const QPointF &v : model->cachedLinkPolyline(e))
                extend(v.x(), v.y());
        }
        if (xMax <= xMin || yMax <= yMin) return;
        const double padX = (xMax - xMin) * 0.15 + 1.0;
        const double padY = (yMax - yMin) * 0.15 + 1.0;
        canvas()->setExtent(MapExtent(xMin - padX, yMin - padY,
                                      xMax + padX, yMax + padY));
    });
    QObject::connect(dlg, &QDialog::accepted, this, [this, dlg, acceptPath]() {
        const int idx = dlg->selectedPathIndex();
        if (idx >= 0 && idx < m_pendingPaths.size()) {
            acceptPath(m_pendingPaths.at(idx));
        } else {
            emit statusMessageChanged(tr("Profile selection cancelled."));
            resetState();
        }
        m_pendingPaths.clear();
        m_pendingPicker = nullptr;
    });
    QObject::connect(dlg, &QDialog::rejected, this, [this]() {
        emit statusMessageChanged(tr("Profile selection cancelled."));
        m_pendingPaths.clear();
        m_pendingPicker = nullptr;
        resetState();
    });
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void OpenSWMMVisMapToolSelectProfile::resetState()
{
    clearInProgress();
    m_acceptedPath = ProfileRouter::Path{};
    if (m_overlay) {
        // Same liveness rule as the destructor — only touch the
        // overlay when its scene is still alive.  If the scene was
        // destroyed first (project teardown), the QGraphicsItem
        // storage is freed and reading scene() would crash.
        if (m_overlayScene)
            m_overlayScene->removeItem(m_overlay);
        if (m_overlayScene)
            delete m_overlay;
        m_overlay      = nullptr;
        m_overlayScene = nullptr;
    }
}

void OpenSWMMVisMapToolSelectProfile::clearInProgress()
{
    m_state     = State::Idle;
    m_startNode = -1;
    m_endNode   = -1;
    m_waypoints.clear();
    if (m_pendingPicker) {
        m_pendingPicker->close();
        m_pendingPicker = nullptr;
    }
    m_pendingPaths.clear();
    // Detach from any in-flight worker — the QtConcurrent task can't be
    // forcibly stopped, but disconnecting + clearing the watcher pointer
    // makes the finished() lambda a no-op when the worker eventually
    // returns (see routeAndPick's "superseded by newer routing" guard).
    if (m_routingWatcher) {
        m_routingWatcher->disconnect();
        m_routingWatcher->deleteLater();
        m_routingWatcher = nullptr;
        emit routingBusyChanged(false);
    }
}

SWMMModelLayer *OpenSWMMVisMapToolSelectProfile::activeModel() const
{
    if (!canvas()) return nullptr;
    for (OpenSWMMVisLayer *l : canvas()->layers()) {
        if (!l) continue;
        if (auto *sl = qobject_cast<SWMMModelLayer *>(l)) return sl;
    }
    return nullptr;
}
