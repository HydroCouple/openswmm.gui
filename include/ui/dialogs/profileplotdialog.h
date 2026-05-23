/*!
 * \file   profileplotdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BC's host dialog for the longitudinal profile plot.
 *
 *         Composition: a `ProfilePlotWidget` (the chart) in the centre, a
 *         `ProfileLayerPanel` on the right (layer toggles), a Sources
 *         panel listing every `SWMMResultsLayer` in the active project's
 *         `AnimationController`.  Animation-cursor sync goes through the
 *         controller's `currentTimeChanged` signal, with each source
 *         resolving its own period via
 *         `SWMMResultsLayer::periodIndexForDateTime`.
 */

#ifndef PROFILE_PLOT_DIALOG_H
#define PROFILE_PLOT_DIALOG_H

#include "plot/profilebuilder.h"
#include "plot/profilerouter.h"
#include "plot/profileplotwidget.h"
#include "plot/profileplotoptions.h"
#include "selection/selectionmanager.h"   // SWMMObjectRef (AT.3)

#include <QDateTime>
#include <QDialog>
#include <QHash>
#include <QPointer>
#include <QSet>
#include <QVector>

#include <memory>

class AnimationController;
class GISRasterLayer;
class MapCanvas;
class ProfileLayerPanel;
class QAction;
class QMenu;
class QProgressBar;
class QToolButton;
class SpatialReferenceSystem;
class SWMMModelLayer;
class SWMMResultsLayer;
class SWMMVisProjectWindow;

class ProfilePlotDialog : public QDialog
{
    Q_OBJECT

public:
    /*!
     * \brief Constructs the dialog and binds it to the given path on the
     *        active model.  Result sources are auto-discovered from
     *        \p anim 's primary and secondary layers; the primary
     *        controller drives the animation cursor.
     */
    /*!
     * \param terrain    Optional DEM raster; when non-null the dialog
     *                   samples it at each path-node coordinate and uses
     *                   the resulting elevations as the ground line.
     *                   Pass `nullptr` to fall back to `invert + maxDepth`.
     * \param canvasSRS  CRS of \p terrain's containing canvas; required
     *                   only when terrain is non-null.
     */
    ProfilePlotDialog(SWMMModelLayer              *model,
                      AnimationController         *anim,
                      const ProfileRouter::Path   &path,
                      SWMMVisProjectWindow        *projectWindow,
                      QWidget                     *parent = nullptr);

    /*!
     * \brief  Sever every external signal/slot connection while children
     *         are still alive so that any cross-window teardown order
     *         (e.g. SWMMVis-level closeEvent → project close → dialog
     *         close all in one event-loop iteration) cannot deliver a
     *         signal into a half-destroyed dialog.
     */
    ~ProfilePlotDialog() override;

signals:
    /*! \brief AT.3 — emitted when the user picks "Plot Time Series…" from
     *  a node / link right-click on the profile. The main window handles
     *  this by routing into `SWMMVis::openComparisonPlotFor(ref)`, so the
     *  profile dialog uses the same modern ComparisonPlotDialog (toolbar,
     *  hover tooltips, stats panel) as every other entry point. */
    void plotTimeSeriesRequested(const SWMMObjectRef &ref);

private slots:
    /*!
     * \brief Re-fetches series for the currently-enabled sources and
     *        pushes them to the plot widget.
     */
    void rebindSources();

    /*!
     * \brief Updates the plot's current-period from the primary
     *        AnimationController's tick.
     */
    void onAnimationTimeChanged(const QDateTime &dt);

    /*!
     * \brief Toggle a single result layer on/off in the Sources dropdown.
     */
    void onSourceActionToggled();

private:
    void buildLayout();
    void populateSourcesPanel();

    /*!
     * \brief Walks each path link's polyline (via SWMMModelLayer's cached
     *        geometry), samples the active terrain raster at densified
     *        positions, and stores the results in
     *        `m_pathStatic.terrainSamples`.  Clears the array when the
     *        user hasn't opted in to terrain ground.
     */
    void rebuildTerrainSamples();

    QPointer<SWMMModelLayer>              m_model;
    QPointer<MapCanvas>                   m_canvas;
    QPointer<SWMMVisProjectWindow>        m_projectWindow;
    QPointer<AnimationController>         m_anim;
    ProfileRouter::Path                   m_routerPath;
    ProfileBuilder::PathStatic            m_pathStatic;

    // Project windows we've subscribed to `aboutToClose` (deduped).  May
    // include the primary `m_projectWindow` plus any project that owns a
    // result layer currently bound as a source.  Raw pointers are safe
    // here because we manually remove entries in our handler before any
    // project teardown begins (see `onObservedProjectAboutToClose`).
    QSet<SWMMVisProjectWindow *>          m_observedProjects;

    void subscribeProjectClose(SWMMVisProjectWindow *pw);
    void onObservedProjectAboutToClose(SWMMVisProjectWindow *pw);

    ProfilePlotWidget                    *m_plot         = nullptr;
    ProfileLayerPanel                    *m_layerPanel   = nullptr;
    QToolButton                          *m_sourceButton = nullptr;
    QMenu                                *m_sourceMenu   = nullptr;
    QProgressBar                         *m_loadingBar   = nullptr;
    int                                   m_loadCookie   = 0;  // ignores stale watcher returns
    ProfilePlotOptions                   *m_options      = nullptr;

    // Layer pointer (non-owning) per menu action.
    QHash<QAction *, QPointer<SWMMResultsLayer>> m_actionLayer;

    // Per-layer cache of computed profile data. Populated lazily by
    // rebindSources(); invalidated on `resultsOpened`,
    // `resultsFilePathChanged`, or layer destruction so subsequent UI
    // option toggles reuse the cached `SourceDerived` instead of
    // refetching the .out file every time. `m_cacheWired` tracks the
    // layers we've already connected invalidation signals on to avoid
    // duplicate connections.
    QHash<SWMMResultsLayer *,
          std::shared_ptr<ProfileBuilder::SourceDerived>> m_sourceCache;
    QSet<SWMMResultsLayer *> m_cacheWired;
    void invalidateSourceCacheFor(SWMMResultsLayer *layer);
    void ensureCacheInvalidationWired(SWMMResultsLayer *layer);
};

#endif // PROFILE_PLOT_DIALOG_H
