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

#include "plot/plotattribute.h"
#include "plot/resultdescriptor.h"
#include "plot/profileattributesampler.h"
#include "plot/profilebuilder.h"
#include "plot/profilerouter.h"
#include "plot/profileplotwidget.h"
#include "plot/profileplotoptions.h"
#include "selection/selectionmanager.h"   // SWMMObjectRef (AT.3)

#include <QDateTime>
#include <QDialog>
#include <QBrush>
#include <QHash>
#include <QPen>
#include <QPointF>
#include <QPointer>
#include <QSet>
#include <QVector>

#include <functional>
#include <memory>

class AnimationController;
class GISRasterLayer;
class MapCanvas;
class ProfileAttributeTrackOptions;
class ProfileAttributeTracksWidget;
class ProfileLayerPanel;
class QAction;
class QMenu;
class QProgressBar;
class QScrollArea;
class QSplitter;
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

protected:
    /*!
     * \brief  Reconciles the attribute-tracks master toggle with the
     *         splitter state that DialogLayoutWatcher restored on this
     *         first Show — restoreState() does not emit splitterMoved, so
     *         a persisted drag-collapsed pane would otherwise disagree
     *         with a restored-checked toggle action.
     */
    void showEvent(QShowEvent *event) override;

    /*!
     * \brief  Watches the tracks widget for resizes so the profile's right
     *         gutter can absorb the tracks scroll area's vertical scrollbar
     *         (see \ref syncTracksGutter).
     */
    bool eventFilter(QObject *watched, QEvent *event) override;

signals:
    /*! \brief Emitted when the user picks a specific variable from the
     *  "Plot Time Series…" submenu on a node / link right-click. Mirrors
     *  the map view's `plotAttributeRequested` so both entry points share
     *  the same ComparisonPlotDialog routing in `SWMMVis`. The descriptor
     *  carries a fixed attribute or a species BY NAME (Y2b-2, amendment
     *  D-Y4); an INVALID descriptor is the "All attributes" sentinel and
     *  the handler fans out one series per variable valid for the kind,
     *  species included. */
    void plotAttributeRequested(const SWMMObjectRef &ref,
                                const openswmmvis::plot::ResultDescriptor &descriptor);

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

    // ── Attribute tracks (synced pane below the profile) ────────────────

    /*! Builds the toolbar attribute menu + master toggle and wires the
     *  tracks pane (x-sync, options, collapse behavior). Called from
     *  buildLayout() after the splitter and both widgets exist. */
    void buildAttributeTracksUi(class QToolBar *toolbar);

    /*! Async rebuild of the tracks pane from the checked sources × visible
     *  attributes — mirrors rebindSources() (worker thread, cookie guard,
     *  per-(layer,attribute) cache). Cheap when everything is cached. */
    void rebuildTracks();

    /*! Shows/collapses the pane per options + master toggle, keeping the
     *  splitter, the toggle action, and auto-hide in agreement. */
    void updateTracksPaneVisibility();

    /*! Pushes the profile widget's virtual-chainage table, margins, x-label
     *  and current range into the tracks widget (call after any setPath). */
    void syncTracksAxes();

    /*! Keeps the profile pane exactly as wide as the tracks widget by giving
     *  the plot's holder a right margin equal to the tracks scroll area's
     *  vertical scrollbar. Both panes derive their pixel column from
     *  `width() - leftGutter - rightGutter`, so unequal widths shift every
     *  column — by a full scrollbar width at the last node. */
    void syncTracksGutter();

    /*!
     * \brief Rebuilds `m_pathStatic.terrainSamples` — the sampled ground
     *        line — per `resolvedGroundSource()`: the 2D mesh vertex
     *        elevations interpolated at the path stations (Mesh2D), the
     *        active terrain raster (TerrainDEM), or nothing (NodeRims, the
     *        widget then draws the rim-to-rim line).
     */
    void rebuildTerrainSamples();

    /*!
     * \brief Walks each path link's polyline (oriented along traversal),
     *        densified every ~5 model units (≤ 20 samples per segment), and
     *        calls \p fn(realChainage, modelX, modelY) at each station. Shared
     *        by the terrain ground line and the 2D inundation overlay.
     */
    void forEachPathStation(
        const std::function<void(double, double, double)> &fn) const;

    /*! Same walk, with each station projected into the canvas CRS and
     *  delivered as a 2D SCENE point (canvas x, −canvas y) — the frame the
     *  mesh / 2D results layers sample in. `fn(realChainage, scenePt)`. */
    void forEachPathStationScene(
        const std::function<void(double, const QPointF &)> &fn) const;

    /*! First SWMM2DMeshLayer on the canvas (the 2D mesh profile's rule), or
     *  nullptr. */
    class SWMM2DMeshLayer *firstMeshLayer() const;

    /*! The option's ground source with `Auto` resolved: 2D mesh when the
     *  project has a mesh layer, else node rims. */
    ProfilePlotOptions::GroundSource resolvedGroundSource() const;

    QPointer<class SWMM2DMeshLayer>       m_groundMesh;        ///< mesh the ground line is sampled from
    ProfilePlotOptions::GroundSource      m_lastGroundSource = ProfilePlotOptions::NodeRims;

    // ── 2D inundation overlay ───────────────────────────────────────────

    /*! One station where the path crosses the active 2D results mesh:
     *  static geometry cached once, depth re-read every animation tick. */
    struct Surface2DStation
    {
        double  chainage = 0.0;   ///< real path chainage
        QPointF scenePt;          ///< 2D layer scene coords (x, -y of canvas CRS)
        int     triIdx   = -1;    ///< containing results cell
        double  bed      = 0.0;   ///< mesh bed elevation at the station
    };

    /*!
     * \brief Rebinds the overlay to the project's active 2D results layer:
     *        resolves the mesh, transforms every path station into the 2D
     *        scene, caches its containing cell + bed, then refreshes the
     *        depths for the current frame. Clears everything when the
     *        option is off or no 2D layer / mesh is available.
     */
    void rebuildSurface2DStations();

    /*! Re-reads the current-frame interpolated depth at every cached station
     *  and pushes bed / WSE samples to the plot. Cheap (no cell search). */
    void refreshSurface2DDepths();

    QVector<Surface2DStation>             m_surface2D;
    QPointer<class SWMM2DResultsLayer>    m_surface2DLayer;
    QAction                              *m_actShow2D = nullptr;

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

    // ── Attribute tracks state ──────────────────────────────────────────
    ProfileAttributeTrackOptions         *m_trackOptions = nullptr;
    ProfileAttributeTracksWidget         *m_tracks       = nullptr;
    QScrollArea                          *m_tracksScroll = nullptr;  ///< the splitter pane; scrolls when many tracks
    QWidget                              *m_plotHolder   = nullptr;  ///< carries the scrollbar-matching right gutter
    QSplitter                            *m_profileSplit = nullptr;
    QMenu                                *m_tracksMenu   = nullptr;
    QAction                              *m_actShowTracks = nullptr;
    /*! The "Link attributes" section header — species node-scope entries
     *  are inserted above it so each scope's species sit with its fixed
     *  attributes. */
    QAction                              *m_tracksLinkSection = nullptr;
    /*! Species entries currently in the Tracks menu — rebuilt whenever
     *  the source set (and so the union of run species) changes. */
    QList<QAction *>                      m_speciesTrackActions;
    int                                   m_trackLoadCookie = 0;
    bool                                  m_syncingX     = false;
    QList<int>                            m_lastSplitSizes;    ///< restore target after collapse

    /*! Rebuilds the species entries of the Tracks menu from the union of
     *  species across the current source layers (Y2b-2 follow-up). */
    void refreshTracksMenuSpecies();

    /*! Per-(layer, variable) sampled-profile cache. The key's string leg
     *  is `"a:<int(PlotAttribute)>"` for fixed attributes and the
     *  scope-qualified species token `"<name>@node"` / `"<name>@link"`
     *  for species tracks. Invalidated by the same signals as
     *  m_sourceCache (see ensureCacheInvalidationWired). */
    QHash<QPair<SWMMResultsLayer *, QString>,
          std::shared_ptr<const ProfileAttributeSampler::AttributeProfile>>
        m_attrCache;

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

    /*!
     * \brief Push a plot-level line style the user just edited onto every
     *        loaded source.
     *
     *        Series styling lives on the results layer (one pen per output
     *        kind per source, so overlaid scenarios stay distinguishable),
     *        while ProfilePlotOptions carries a plot-level set of the same
     *        pens. Nothing read the latter, so editing HGL/EGL/Max-HGL/
     *        Max-EGL in the options tree silently did nothing.
     *
     *        `ProfilePlotOptions::changed` is one signal for every property,
     *        so a blind push would restyle all sources whenever any unrelated
     *        option (a label toggle, say) changed — quietly undoing per-source
     *        customisation. Hence the snapshot below: only the pens that
     *        actually differ from what was last seen get pushed.
     *
     * \returns true when at least one source was restyled.
     */
    bool pushEditedPlotStylesToSources();

    /*! Last-seen plot-level styles, so an edit can be told from a repaint.
     *  Seeded from the options object when it is bound. */
    struct PlotStyleSnapshot {
        QPen   hglLinePen;
        QBrush hglFillBrush;
        QPen   eglLinePen;
        QPen   maxHglLinePen;
        QBrush maxHglFillBrush;
        QPen   maxEglLinePen;
        bool   seeded = false;
    } m_lastPlotStyle;
};

#endif // PROFILE_PLOT_DIALOG_H
