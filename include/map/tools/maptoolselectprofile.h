/*!
 * \file   maptoolselectprofile.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BC's map tool for picking the two endpoints of a profile
 *         path.  Routes between them via ProfileRouter, shows candidates
 *         via ProfilePathOverlay, and (on multi-path) launches a modal
 *         picker dialog.
 *
 *         Input idioms (both accept either a node or a link at each pick;
 *         link-clicks snap to the link endpoint nearest the cursor):
 *
 *           A. Click + modifier — first click sets start, `Ctrl`/`⌘`+click
 *              sets end.
 *           B. Modifier + modifier — `Ctrl`/`⌘`+click for both clicks
 *              (mirrors the QGIS routing-tool idiom).
 *
 *         `Shift+Ctrl/⌘`-click between start and end captures an
 *         intermediate waypoint (router runs `kShortestPathsThrough`).
 *         `Escape` or right-click cancels and resets state.
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
template<typename T> class QFutureWatcher;

class OpenSWMMVisMapToolSelectProfile : public OpenSWMMVisMapTool
{
    Q_OBJECT

public:
    explicit OpenSWMMVisMapToolSelectProfile(MapCanvas *canvas,
                                             QObject    *parent = nullptr);
    ~OpenSWMMVisMapToolSelectProfile() override;

    void activate()   override;
    void deactivate() override;

    void mousePressEvent(QMouseEvent *event) override;
    void keyPressEvent  (QKeyEvent   *event) override;

    /*!
     * \brief Path that the user accepted from the picker (or the only
     *        candidate, when the router emitted exactly one).  Empty
     *        nodes/edges arrays mean "no acceptance yet".
     */
    [[nodiscard]] ProfileRouter::Path acceptedPath() const;

    /*!
     * \brief Drop the accepted path, the on-map overlay, and any
     *        in-progress capture, then invalidate the canvas so the
     *        change is visible. Safe to call when the tool is not the
     *        active canvas tool — wired from the project window when
     *        the user switches away from profile mode and clicks on the
     *        map, so the leftover overlay doesn't follow them around.
     */
    void clearSelection();

signals:
    /*!
     * \brief Fired immediately after the user accepts a path (or after a
     *        single-candidate route auto-confirms).  The path's node and
     *        edge indices are engine indices on the active SWMMModelLayer.
     */
    void profilePathSelected(const ProfileRouter::Path &path);

    /*!
     * \brief Fired when the tool's status string changes — host can pipe
     *        this into the main-window status bar.
     */
    void statusMessageChanged(const QString &message);

    /*!
     * \brief Fired when async routing starts (\p busy = true) and again
     *        when it completes (\p busy = false).  Hosts can wire this
     *        to a status-bar busy spinner so the user sees that
     *        background work is in flight.
     */
    void routingBusyChanged(bool busy);

private:
    enum class State {
        Idle,           /*!< no endpoints captured yet */
        AwaitingEnd,    /*!< start captured; waiting for Ctrl/⌘+click */
        PickingPath,    /*!< both endpoints captured; routing / picker open */
    };

    // Returns the engine node index for a click at (mapX, mapY), snapping
    // through a link's nearest endpoint if a link is hit.  Returns -1 on
    // miss.  `tolerance` is in map units.
    [[nodiscard]] int resolveNodeAt(double mapX, double mapY, double tolerance) const;

    // Computes the click tolerance in map units for the current viewport
    // (12-pixel target like MapToolSelect).
    [[nodiscard]] double clickTolerance() const;

    // Materializes the candidate paths via ProfileNetworkAdapter +
    // ProfileRouter on a worker thread.  Returns immediately after
    // kicking off the future and showing a busy progress dialog; the
    // post-routing flow continues in onRoutingComplete().
    void routeAndPick();

    // Continuation invoked on the main thread once the worker thread's
    // ProfileRouter call finishes (or is cancelled).  Handles the
    // single-path auto-accept and multi-path picker dialog flows.
    void onRoutingComplete(const ProfileRouter::Result &result,
                           int graphNodeCount, int graphEdgeCount);

    // Tears down the overlay, clears endpoints/waypoints, returns to Idle.
    void resetState();

    // Lighter cleanup that clears only the in-progress endpoint capture +
    // any open picker, while leaving the persisted accepted path and its
    // overlay alone.  Used by `activate()` / `deactivate()` so toggling
    // the tool off and back on preserves the previously-selected profile.
    void clearInProgress();

    SWMMModelLayer        *activeModel() const;

    State                  m_state       = State::Idle;
    int                    m_startNode   = -1;     // engine node index
    int                    m_endNode     = -1;
    QVector<int>           m_waypoints;            // engine node indices, in order
    ProfilePathOverlay        *m_overlay      = nullptr;
    // QPointer to the scene that owns m_overlay — used as a liveness
    // guard in the tool destructor / resetState() since QGraphicsItem
    // is not a QObject and we cannot QPointer the overlay itself.  If
    // the scene was destroyed first (typical project-teardown order),
    // the overlay is already freed and we must not touch it.
    QPointer<QGraphicsScene>   m_overlayScene;
    ProfileRouter::Path        m_acceptedPath;

    // Non-modal picker state — kept on the tool so the map remains
    // interactive while the dialog is open.  Cleared in `resetState()`
    // and on dialog accept/reject.
    QPointer<ProfilePathPickerDialog> m_pendingPicker;
    QVector<ProfileRouter::Path>      m_pendingPaths;

    // Async routing state — the worker future.  Parented to `this` so it
    // cleans up when the tool dies.  Status-bar busy indication is
    // surfaced via the routingBusyChanged() signal rather than a local
    // progress dialog so the user can keep panning/zooming the map.
    QFutureWatcher<ProfileRouter::Result> *m_routingWatcher = nullptr;
};

#endif // MAPTOOLSELECTPROFILE_H
