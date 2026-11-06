/**
 * @file      maptoolselectprofile.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BC's map tool for picking the two endpoints of a profile
 *         path.  Routes between them via ProfileRouter, shows candidates
 *         via ProfilePathOverlay, and (on multi-path) launches a nonmodal
 *         picker dialog.
 *
 *         Input idioms (both accept either a node or a link at each pick;
 *         link-clicks snap to the link endpoint nearest the cursor):
 *
 *           A. Click + modifier — first click sets start, next unshifted click sets end.
 *           B. Modifier + modifier — `Ctrl`/`⌘`+click for both clicks
 *              (mirrors the QGIS routing-tool idiom).
 *
 *         `Shift`-click between start and end captures an
 *         intermediate waypoint (router runs `kShortestPathsThrough`).
 *         `Escape` or right-click cancels and resets state.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef MAPTOOLSELECTPROFILE_H
#define MAPTOOLSELECTPROFILE_H

#include "map/tools/maptool.h"
#include "plot/profilerouter.h"

#include <QPointer>
#include <QVector>

class SWMMModelLayer;
class ProfilePathOverlay;
class ProfilePathPickerDialog;
class QGraphicsScene;
template <typename T> class QFutureWatcher;

class OpenSWMMVisMapToolSelectProfile : public OpenSWMMVisMapTool
{
    Q_OBJECT

public:
    /**
     * @brief Create inactive endpoint/profile-route session without installing on canvas.
     * @param[in] canvas Optional borrowed raw canvas, must outlive tool/UI callbacks.
     * @param[in] parent Optional QObject owner, otherwise caller owns tool.
     * @note GUI-thread only except value-snapshot routing worker. Overlay retains raw model; caller
     *   keeps chosen model/topology/engine stable through pending routing, picker and accepted-path
     *   use. Tool does not verify engine generation on completion.
     */
    explicit OpenSWMMVisMapToolSelectProfile(MapCanvas *canvas, QObject *parent = nullptr);
    /**
     * @brief Detach/delete owned profile overlay, removing legacy scene membership only when
     *   tracked scene survives.
     * @note Tracked QGraphicsScene guards canvas-overlay detach/removal, but tool deletes overlay
     *   itself regardless. QObject destroys watcher/connections; worker uses value graph snapshot
     *   and may finish independently, no cancellation/wait guarantee or final
     *   routingBusyChanged(false) here. Pending floating picker is not explicitly closed in
     *   destructor; parent-window ownership controls it after connections die.
     */
    ~OpenSWMMVisMapToolSelectProfile() override;

    /**
     * @brief Activate base, clear in-progress capture and seed up to2unique typed selected nodes
     *   from first model.
     * @note Two seeded nodes drop accepted path and route immediately; one seeds start and clears
     *   overlay paths while acceptedPath value may remain; none shows prior accepted
     *   status/instructions. Normal idle accepted overlay persists across tool toggle. Closing a
     *   pending picker during cleanup can invoke rejection/reset and remove it instead. Overlay
     *   created lazily with current model, existing overlay is not rebound if first model changes.
     */
    void activate() override;
    /**
     * @brief Clear endpoint/waypoint capture and pending UI/watcher, then deactivate base.
     * @note Normally retains accepted data/overlay; pending picker close can trigger rejected
     *   callback/resetState and clear them. In-flight worker is detached, not stopped; no explicit
     *   canvas invalidation on this method.
     */
    void deactivate() override;

    /**
     * @param[in] event Required borrowed mouse event; right resets, left picks endpoint; no release
     *   arming.
     * @brief Idle left sets start; awaiting-end Shift adds waypoint, otherwise different hit sets
     *   end and routes.
     * @note Control modifier is NOT required by current flow despite older input guide. Node or
     *   link endpoint resolves through shared output/model pick; same start/end refused, gage/catch
     *   not endpoints. No hit while idle accepted clears profile; other misses keep capture and
     *   show hint. While routing/picker, click only raises pending picker and status.
     * @note Right clears full state/accepted overlay. Does not select/edit model objects, accept
     *   event or push undo. Node/model identities from first model must correspond to picked
     *   output/name identities.
     */
    void mousePressEvent(QMouseEvent *event) override;
    /**
     * @param[in] event Required borrowed key; Escape resets all, others ignored.
     * @brief Cancel full profile session on Escape and emit status without accepting key or
     *   switching tool. Detached worker can continue.
     */
    void keyPressEvent(QKeyEvent *event) override;

    /*!
     * \brief Path that the user accepted from the picker (or the only
     *        candidate, when the router emitted exactly one).  Empty
     *        nodes/edges arrays mean "no acceptance yet".
     * @return Value copy of latest accepted router path in captured model engine node/link indices;
     *   empty initially/reset. No validation against current engine/topology and no lifetime pin;
     *   copy before independent use.
     */
    [[nodiscard]] ProfileRouter::Path acceptedPath() const;

    /*!
     * \brief Drop the accepted path, the on-map overlay, and any
     *        in-progress capture, then invalidate the canvas so the
     *        change is visible. Safe to call when the tool is not the
     *        active canvas tool — wired from the project window when
     *        the user switches away from profile mode and clicks on the
     *        map, so the leftover overlay doesn't follow them around.
     * @note No-op only when no accepted links/capture/overlay/waypoints; otherwise resetState
     *   detaches async/UI and deletes overlay, invalidate Scene/Overlay and emit ended status. Does
     *   not clear model element selection or undo. Safe inactive provided raw canvas/model
     *   lifetimes remain coherent.
     */
    void clearSelection();

signals:
    /**
     * @param[in] layer Borrowed output layer selected by shared network hit, consumer guards
     *   retention.
     * @note Emitted during endpoint resolution as soon as results hit chosen, even if category
     *   later rejected as unroutable. No routing/model/output binding validated here.
     */
    void resultsLayerPicked(class SWMMResultsLayer *layer);
    /*!
     * \brief Fired immediately after the user accepts a path (or after a
     *        single-candidate route auto-confirms).  The path's node and
     *        edge indices are engine indices on the active SWMMModelLayer.
     * @param[in] path Accepted value path with current-session engine node/link IDs and traversal
     *   directions; consumer copies before retaining asynchronously.
     * @note Single route autoaccepts, multiple routes require nonmodal picker choice. Emitted after
     *   accepted overlay reduction/state Idle/waypoint clear, no model selection change. Stored
     *   endpoint ints retained until cleanup; topology stability caller responsibility.
     */
    void profilePathSelected(const ProfileRouter::Path &path);

    /*!
     * \brief Fired when the tool's status string changes — host can pipe
     *        this into the main-window status bar.
     * @param[in] message Translated capture/routing/error/picker/accepted/ended status.
     * @note Synchronous GUI signal; some cleanup/deactivate paths do not emit empty status, host
     *   determines display lifetime.
     */
    void statusMessageChanged(const QString &message);

    /*!
     * \brief Fired when async routing starts (\p busy = true) and again
     *        when it completes (\p busy = false).  Hosts can wire this
     *        to a status-bar busy spinner so the user sees that
     *        background work is in flight.
     * @param[in] busy True after graph snapshot when worker dispatch starts, false on watcher
     *   completion or explicit detachment.
     * @note False means UI no longer awaits that watcher, not that detached QtConcurrent worker has
     *   physically stopped. Destructor does not promise final false; graph construction itself runs
     *   synchronously before true.
     */
    void routingBusyChanged(bool busy);

private:
    enum class State {
        Idle,        /*!< no endpoints captured yet */
        AwaitingEnd, /*!< start captured; waiting for Ctrl/⌘+click */
        PickingPath, /*!< both endpoints captured; routing / picker open */
    };

    // Returns the engine node index for a click at (mapX, mapY), snapping
    // through a link's nearest endpoint if a link is hit.  Returns -1 on
    // miss.  `tolerance` is in map units.
    /**
     * @param[in] mapX Canvas-map X.
     * @param[in] mapY Canvas-map Y.
     * @param[in] tolerance Nonnegative finite canvas-map radius for shared picking and geographic
     *   fallback.
     * @return First-model engine node ID by picked name, or nearest canvas-projected endpoint of
     *   picked link (upstream wins equal distance), minus one no eligible match. Catch/gage
     *   rejected; if only one endpoint readable chooses it.
     * @note Shared pick prioritizes outputs/model and may emit resultsLayerPicked. On total miss
     *   for visible first GEOGRAPHIC model, scans forward-projected native nodes to recover
     *   equivalent longitudes, strict radius/tie, typed visibility check. Link endpoint transform
     *   statuses ignored. No first-model vs picked-layer identity check, engine mutation or
     *   selection.
     */
    [[nodiscard]] int resolveNodeAt(double mapX, double mapY, double tolerance);

    // Computes the click tolerance in map units for the current viewport
    // (12-pixel target like MapToolSelect).
    /**
     * @return Max axis canvas-map delta for integer-converted effective pixels=max(preference click
     *   radius, largest visible model marker halfbound+4). Fallback1 when no canvas/nonpositive
     *   width; invalid extent conversion can give0. Not fixed12pixels despite old helper comment;
     *   no hit/model mutation.
     */
    [[nodiscard]] double clickTolerance() const;

    // Materializes the candidate paths via ProfileNetworkAdapter +
    // ProfileRouter on a worker thread.  Returns immediately after
    // kicking off the future and emitting status-bar busy signals; the
    // post-routing flow continues in onRoutingComplete().
    /**
     * @brief Snapshot current first-model routing graph/inputs on GUI thread then dispatch
     *   value-only shortest-path worker.
     * @note Missing model/endpoints resets. Uses undirected routing, preference maxPaths, no soft
     *   time cap, max int iterations; ordered waypoints force one shortest route per leg. Refreshes
     *   endpoint overlay and processes non-user events before watcher dispatch. Prior watcher
     *   disconnected/deleteLater, emits busy false; new busy true/status then QtConcurrent run.
     * @note Worker captures graph/indices/options ONLY and survives tool deletion safely.
     *   Completion GUI slot reads result only if watcher still current, emits busy false and
     *   onRoutingComplete. Does not cancel/wait old tasks or validate model generation/topology
     *   still matches snapshot; caller prevents incompatible edits. No modal progress dialog.
     */
    void routeAndPick();

    // Continuation invoked on the main thread once the worker thread's
    // ProfileRouter call finishes (or is cancelled).  Handles the
    // single-path auto-accept and multi-path picker dialog flows.
    /**
     * @param[in] result Value router result for current watcher, paths may reference earlier
     *   captured topology.
     * @param[in] graphNodeCount Captured graph node count for status diagnostics only.
     * @param[in] graphEdgeCount Captured graph edge count for status diagnostics only.
     * @brief Handle GUI completion: error/empty reset full state, one path autoaccept, multiple
     *   paths nonmodal floating picker.
     * @note Uses CURRENT first model without snapshot identity check. Acceptance stores path,
     *   reduces overlay to highlighted candidate/endpoints, state Idle/waypoints clear and emits
     *   selected/status. Multiple picker hover changes overlay, accept/reject resolve pending list;
     *   no engine edits.
     * @note Picker zoom bounds derive native node/link coordinates and pass directly to canvas
     *   extent with15%+1padding (no reprojection); model/canvas frame compatibility required.
     *   Retains raw model in zoom callback; caller preserves lifetime. Closing picker can invoke
     *   rejection reset.
     */
    void onRoutingComplete(const ProfileRouter::Result &result, int graphNodeCount,
                           int graphEdgeCount);

    // Tears down the overlay, clears endpoints/waypoints, returns to Idle.
    /**
     * @brief Clear capture/pending picker/watcher, empty accepted path, detach/delete overlay and
     *   clear scene guard.
     * @note Does not itself invalidate canvas or emit status except indirect picker rejection/busy
     *   detachment signals; caller normally requests redraw. Detached worker continues. No model
     *   selection/engine changes.
     */
    void resetState();

    // Lighter cleanup that clears only the in-progress endpoint capture +
    // any open picker, while leaving the persisted accepted path and its
    // overlay alone.  Used by `activate()` / `deactivate()` so toggling
    // the tool off and back on preserves the previously-selected profile.
    /**
     * @brief Set Idle, clear endpoint/waypoint/pending paths, close picker and
     *   disconnect/deleteLater current watcher with busy false.
     * @note Does not directly clear acceptedPath/overlay or stop worker. However closing picker can
     *   emit rejected callback that calls resetState and clears them. No explicit canvas redraw or
     *   host status here.
     */
    void clearInProgress();

    /**
     * @return Borrowed FIRST nonnull SWMMModelLayer in canvas list regardless
     *   visibility/opacity/engine readiness/selection, null no canvas/model. No ownership transfer
     *   or identity pin.
     */
    SWMMModelLayer *activeModel() const;

    State m_state = State::Idle;
    int m_startNode = -1; // engine node index
    int m_endNode = -1;
    QVector<int> m_waypoints; // engine node indices, in order
    ProfilePathOverlay *m_overlay = nullptr;
    // QPointer to the scene that owns m_overlay — used as a liveness
    // guard in the tool destructor / resetState() since QGraphicsItem
    // is not a QObject and we cannot QPointer the overlay itself.  If
    // the scene was destroyed first (typical project-teardown order),
    // the overlay is already freed and we must not touch it.
    QPointer<QGraphicsScene> m_overlayScene;
    ProfileRouter::Path m_acceptedPath;

    // Non-modal picker state — kept on the tool so the map remains
    // interactive while the dialog is open.  Cleared in `resetState()`
    // and on dialog accept/reject.
    QPointer<ProfilePathPickerDialog> m_pendingPicker;
    QVector<ProfileRouter::Path> m_pendingPaths;

    // Async routing state — the worker future.  Parented to `this` so it
    // cleans up when the tool dies.  Status-bar busy indication is
    // surfaced via the routingBusyChanged() signal rather than a local
    // progress dialog so the user can keep panning/zooming the map.
    QFutureWatcher<ProfileRouter::Result> *m_routingWatcher = nullptr;
};

#endif // MAPTOOLSELECTPROFILE_H
