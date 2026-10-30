/**
 * @file      profileplotdialog.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BC's host dialog for the longitudinal profile plot.
 *
 *         Composition: a `ProfilePlotWidget` (the chart) in the centre, a
 *         `ProfileLayerPanel` on the right (layer toggles), a Sources
 *         panel listing every `SWMMResultsLayer` in the active project's
 *         `AnimationController`.  Animation-cursor sync goes through the
 *         controller's `currentTimeChanged` signal, with each source
 *         resolving its own period via
 *         `SWMMResultsLayer::periodIndexForDateTime`.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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
#include "selection/selectionmanager.h" // SWMMObjectRef (AT.3)

#include <QDateTime>
#include "ui/panels/plotpanelview.h"
#include <QBrush>
#include <QHash>
#include <QPointer>
#include <QTimer>
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

class ProfilePlotDialog : public openswmmvis::ui::PlotPanelView
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
     * Historical terrain input: optional DEM raster; when non-null the dialog
     *                   samples it at each path-node coordinate and uses
     *                   the resulting elevations as the ground line.
     *                   Pass `nullptr` to fall back to `invert + maxDepth`.
     * Historical canvasSRS input: CRS of the terrain canvas; required
     *                   only when terrain is non-null.
     */
    /**
     * @brief Build a GUI-thread longitudinal profile panel from a copied authored route.
     * @param[in,out] model Nullable borrowed model, QPointer-guarded; static route geometry is
     * materialized synchronously via ProfileNetworkAdapter.
     * @param[in,out] anim Nullable borrowed animation controller, guarded; discovers result sources
     * and drives current-period/cursor updates.
     * @param[in] path Borrowed engine-index route copied into the panel; source/model identity must
     * match it.
     * @param[in,out] projectWindow Nullable borrowed owner of canvas, units, active terrain and
     * primary data; observed project close invalidates callbacks and closes this plot.
     * @param[in] parent Optional Qt owner.
     * Owns plot/options/tracks/widgets and future watchers; borrowed layers remain external.
     * Restores app-wide track options and per-project source styles/visibility, starts asynchronous
     * profile fetches and samples terrain/2D overlays. Plot selections can change model/map
     * selection; option edits can change source style/settings. No global cancel rollback is held.
     */
    ProfilePlotDialog(SWMMModelLayer *model, AnimationController *anim,
                      const ProfileRouter::Path &path, SWMMVisProjectWindow *projectWindow,
                      QWidget *parent = nullptr);

    /*!
     * \brief  Sever every external signal/slot connection while children
     *         are still alive so that any cross-window teardown order
     *         (e.g. SWMMVis-level closeEvent → project close → dialog
     *         close all in one event-loop iteration) cannot deliver a
     *         signal into a half-destroyed dialog.
     */
    /**
     * @brief Disconnect external clock/model/canvas/project/source routes before member and widget
     * teardown.
     * Clears menu layer references/observed projects; does not own those objects or explicitly
     * cancel/join QtConcurrent work. Workers retain copied jobs/path/shared cache values, and GUI
     * result delivery is context/cookie guarded.
     */
    ~ProfilePlotDialog() override;
    /**
     * @brief Borrow the originating project window.
     * @return Guarded pointer or nullptr after project destruction/primary close; ownership stays
     * external.
     */
    SWMMVisProjectWindow *projectWindow() const;

protected:
    /**
     * @brief Catch up the cursor/primary period and active 2D overlay when activated.
     * @param[in] active New plot activation; false performs no refresh.
     */
    void plotActivationChanged(bool active) override;
    /*!
     * \brief  After DialogLayoutWatcher restores the splitter state on this
     *         first Show, makes sure a shown attribute-tracks pane actually
     *         has height (a persisted zero-height split would otherwise
     *         read as the tracks never appearing).
     */
    /**
     * @brief Run base show/layout behavior and catch up active 2D time.
     * @param[in,out] event Required borrowed Qt event forwarded to PlotPanelView.
     * Queues a context-bound zero-delay tracks expansion after layout restoration, without forcing
     * hidden tracks visible.
     */
    void showEvent(QShowEvent *event) override;

    /*!
     * \brief  Watches the tracks widget for resizes so the profile's right
     *         gutter can absorb the tracks scroll area's vertical scrollbar
     *         (see \ref syncTracksGutter).
     */
    /**
     * @brief Resynchronize scrollbar gutter on tracks-widget resize while preserving normal
     * delivery.
     * @param[in] watched Borrowed QObject event target.
     * @param[in] event Required borrowed Qt event, read synchronously.
     * @return Base PlotPanelView event-filter result; no event ownership transfer.
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
    /**
     * @brief Request external comparison time-series routing for a profile node/link.
     * @param[in] ref Borrowed signal object identity; listeners copy if retained.
     * @param[in] descriptor Fixed/species variable descriptor; invalid is the All attributes
     * sentinel.
     * Signal itself does not open a chart; map selection was applied by the originating menu
     * action.
     */
    void plotAttributeRequested(const SWMMObjectRef &ref,
                                const openswmmvis::plot::ResultDescriptor &descriptor);

private slots:
    /*!
     * \brief Re-fetches series for the currently-enabled sources and
     *        pushes them to the plot widget.
     */
    /**
     * @brief Advance the source-generation cookie and asynchronously rebuild enabled profile
     * bindings.
     * Snapshots GUI source/style/options/path and shared cache values; workers fetch missing series
     * and compute with kGravityFps2. Accepted cookie-matching completion merges caches, updates
     * plot/cursor/axes and tracks; stale results are discarded without canceling their computation.
     * No checked sources clears plotted series. In-flight count prevents mutable live appends until
     * all source fetches finish, then catches up readable tails.
     */
    void rebindSources();

    /*!
     * \brief Updates the plot's current-period from the primary
     *        AnimationController's tick.
     */
    /**
     * @brief Update profile and tracks periods from the primary results controller.
     * @param[in] dt Borrowed requested timestamp resolved by primary periodIndexForDateTime; no-op
     * without clock/primary.
     * Updates LID states and advances the active 2D overlay as-of this time even when hidden on the
     * map; inactive plots do not drive the 2D layer.
     */
    void onAnimationTimeChanged(const QDateTime &dt);
    /**
     * @brief Read primary result LID cell states for path nodes when either LID overlay is enabled.
     * Respects the primary source checkbox, scales bottom/top/pressure/head between feet/metres to
     * model FLOW_UNITS, and reports unavailable/hidden source status. Updates only plot
     * state/action visibility, not simulation values.
     */
    void refreshLidWaterStates();

    /*!
     * \brief Toggle a single result layer on/off in the Sources dropdown.
     */
    /**
     * @brief Update source-count caption, persist per-project source visibility/color/name/style
     * and rebind enabled sources.
     * Settings array replaces prior records; pathless layers are omitted. Uses guarded action-layer
     * references and normal asynchronous rebuild semantics.
     */
    void onSourceActionToggled();

private:
    /**
     * @brief Construct owned profile/tracks UI, toolbar, options/export/context routes and loading
     * indicator once.
     * Synchronizes options and track axes, samples ground/2D overlays, binds map/model selection,
     * and observes model/terrain changes. PNG export renders current widgets and ignores
     * pixmap-save return; context Properties opens display options, while selection/zoom actions
     * affect the model/canvas.
     */
    void buildLayout();
    /**
     * @brief Rebuild source-menu actions from controller/project/canvas discovery and stored
     * settings.
     * Reapplies saved source scenario names/colors/profile pens, creates checked actions and
     * project/layer lifetime observers, refreshes species tracks. Menu actions own their UI
     * lifetime; layers remain QPointer-borrowed. Existing menu-item pointers are invalidated.
     */
    void populateSourcesPanel();

    // ── Attribute tracks (synced pane below the profile) ────────────────

    /*! Builds the toolbar attribute menu + master toggle and wires the
     *  tracks pane (x-sync, options, collapse behavior). Called from
     *  buildLayout() after the splitter and both widgets exist. */
    /**
     * @brief Install attribute/species menu, master toggle and synchronized track/options routes.
     * @param[in,out] toolbar Required borrowed owned-toolbar instance receiving new Qt-owned
     * buttons/actions.
     * Persists track options app-wide, blocks menu feedback during hydration, links virtual X
     * ranges bidirectionally and restores expanded splitter proportions.
     */
    void buildAttributeTracksUi(class QToolBar *toolbar);

    /*! Async rebuild of the tracks pane from the checked sources × visible
     *  attributes — mirrors rebindSources() (worker thread, cookie guard,
     *  per-(layer,attribute) cache). Cheap when everything is cached. */
    /**
     * @brief Asynchronously fetch visible fixed/species attribute profiles for checked source
     * layers.
     * Uses copied path/jobs/specs/shared immutable cache snapshot and a generation cookie at
     * nonempty job submission; accepted completion merges per-layer/descriptor caches and sets
     * current period. Unknown species are skipped, first checked source is promoted for envelopes
     * when primary is absent. Empty requests clear tracks synchronously; this early-return path
     * does not increment the worker cookie. Workers are not canceled by superseding requests.
     */
    void rebuildTracks();

    /*! Shows/collapses the pane per options + master toggle, keeping the
     *  splitter, the toggle action, and auto-hide in agreement. */
    /**
     * @brief Show tracks only when options select any variable and the master toggle permits it.
     * Disables the toggle with no selected variables, expands shown panes and aligns scrollbar
     * gutter; does not change chosen attributes.
     */
    void updateTracksPaneVisibility();

    /*! Gives a shown tracks pane real height: restores the last user split
     *  when the current one is zero / a sliver (persisted collapsed state,
     *  first show), else a 3:1 default. No-op when already expanded. */
    /**
     * @brief Expand a visible tracks split smaller than 72 pixels, preserving an already adequate
     * split.
     * Restores last valid two-pane sizes or a 3:1 fallback with minimum 72-pixel tracks; missing
     * controls/shape mismatch is ignored.
     */
    void ensureTracksPaneExpanded();

    /*! Pushes the profile widget's virtual-chainage table, margins, x-label
     *  and current range into the tracks widget (call after any setPath). */
    /**
     * @brief Copy plot virtual-chainage table, margins, range and unit label to tracks.
     * Installs a QPointer-guarded virtual-to-real mapper that returns its input if the plot dies,
     * and synchronizes scrollbar gutter.
     */
    void syncTracksAxes();

    /*! Keeps the profile pane exactly as wide as the tracks widget by giving
     *  the plot's holder a right margin equal to the tracks scroll area's
     *  vertical scrollbar. Both panes derive their pixel column from
     *  `width() - leftGutter - rightGutter`, so unequal widths shift every
     *  column — by a full scrollbar width at the last node. */
    /**
     * @brief Match profile right layout margin to the visible tracks scrollbar width.
     * Keeps other margins; hidden tracks restore zero right deficit. Missing holder/scroll/layout
     * is ignored.
     */
    void syncTracksGutter();

    /*!
     * \brief Rebuilds `m_pathStatic.terrainSamples` — the sampled ground
     *        line — per `resolvedGroundSource()`: the 2D mesh vertex
     *        elevations interpolated at the path stations (Mesh2D), the
     *        active terrain raster (TerrainDEM), or nothing (NodeRims, the
     *        widget then draws the rim-to-rim line).
     */
    /**
     * @brief Clear prior ground samples/subscriptions and resample the selected ground source.
     * Mesh uses finite interpolated elevations at canvas-scene stations and subscribes to
     * edits/geometry/attributes; DEM samples band one in canvas CRS with Y unflipped and applies
     * project terrain vertical factor. NodeRims or unavailable source leaves no samples. No
     * terrain/model elevations are modified.
     */
    void rebuildTerrainSamples();

    /*!
     * \brief Walks each path link's polyline (oriented along traversal),
     *        densified every ~5 model units (≤ 20 samples per segment), and
     *        calls \p fn(realChainage, modelX, modelY) at each station. Shared
     *        by the terrain ground line and the 2D inundation overlay.
     */
    /**
     * @brief Walk oriented link polylines and synchronously invoke a station callback.
     * @param[in] fn Required nonempty callable receiving authored real chainage and model-CRS X/Y
     * coordinates (model linear units); copied references are not retained.
     * Requires route link count plus one equal node count and static chainage count equal node
     * count, otherwise no calls. Skips empty/zero-length geometry, scales geometric distance to
     * authored link spans, and uses 1..20 subdivisions per segment near five model units, emitting
     * both endpoints (shared endpoints can repeat).
     */
    void forEachPathStation(const std::function<void(double, double, double)> &fn) const;

    /*! Same walk, with each station projected into the canvas CRS and
     *  delivered as a 2D SCENE point (canvas x, −canvas y) — the frame the
     *  mesh / 2D results layers sample in. `fn(realChainage, scenePt)`. */
    /**
     * @brief Project path stations into the canvas scene frame before synchronous callback
     * delivery.
     * @param[in] fn Required nonempty callable receiving authored chainage and borrowed QPointF
     * (canvas X, negated canvas Y); not retained.
     * No-op without canvas/model. Creates/destroys an OGR transform when CRS differs;
     * failed/unavailable transforms retain original model coordinates before Y inversion rather
     * than reporting failure.
     */
    void forEachPathStationScene(const std::function<void(double, const QPointF &)> &fn) const;

    /*! First SWMM2DMeshLayer on the canvas (the 2D mesh profile's rule), or
     *  nullptr. */
    /**
     * @brief Find the first mesh layer in current canvas order.
     * @return Borrowed canvas-layer pointer or nullptr; ownership/lifetime stays with its project.
     */
    class SWMM2DMeshLayer *firstMeshLayer() const;

    /*! The option's ground source with `Auto` resolved: 2D mesh when the
     *  project has a mesh layer, else node rims. */
    /**
     * @brief Resolve automatic ground selection without sampling.
     * @return Explicit configured source, or Mesh2D when a canvas mesh exists, otherwise NodeRims.
     * Auto does not select the active terrain raster.
     */
    ProfilePlotOptions::GroundSource resolvedGroundSource() const;

    QPointer<class SWMM2DMeshLayer> m_groundMesh; ///< mesh the ground line is sampled from
    ProfilePlotOptions::GroundSource m_lastGroundSource = ProfilePlotOptions::NodeRims;

    // ── 2D inundation overlay ───────────────────────────────────────────

    /*! One station where the path crosses the active 2D results mesh:
     *  static geometry cached once, depth re-read every animation tick. */
    struct Surface2DStation {
        double chainage = 0.0;   ///< real path chainage
        QPointF scenePt;         ///< 2D layer scene coords (x, -y of canvas CRS)
        int displayTriIdx = -1;  ///< containing results storage triangle
        int boundaryTriIdx = -1; ///< opposite side of a shared-edge trace
        double bed = 0.0;        ///< result bed in profile elevation units
        bool breakBefore = false;
    };

    /*!
     * \brief Rebinds the overlay to the project's active 2D results layer:
     *        transforms the path into the 2D scene, caches exact storage-
     *        triangle intervals and result bed, then refreshes the
     *        depths for the current frame. Clears everything when the
     *        option is off or no 2D results layer is available.
     */
    /**
     * @brief Replace overlay bindings with exact storage-triangle intervals from active 2D results.
     * Clears/disconnects old state; missing option/context/source clears plotted overlay. Maps
     * sampled scene distance back to authored link chainage, stores results-bed elevations/revision
     * and signed shoreline geometry, then binds frame/policy/destruction refreshes. Uses results
     * bed rather than unrelated DEM/terrain.
     */
    void rebuildSurface2DStations();

    /*! Re-reads the current-frame interpolated depth at every cached station
     *  and pushes bed / WSE samples to the plot. Cheap (no cell search). */
    /**
     * @brief Publish cached station bed plus signed current water depth in mesh elevation units.
     * Revision mismatch rebuilds stations; missing layer/stations publishes an empty overlay.
     * Applies depthToMeshUnits once and preserves discontinuity markers; no cell search on the
     * matching cached geometry path.
     */
    void refreshSurface2DDepths();

    QVector<Surface2DStation> m_surface2D;
    quint64 m_surface2DGeometryRevision = 0;
    QPointer<class SWMM2DResultsLayer> m_surface2DLayer;
    QAction *m_actShow2D = nullptr;
    QAction *m_actLidWater = nullptr;
    QAction *m_actLidHead = nullptr;

    QPointer<SWMMModelLayer> m_model;
    QPointer<MapCanvas> m_canvas;
    QPointer<SWMMVisProjectWindow> m_projectWindow;
    QPointer<AnimationController> m_anim;
    ProfileRouter::Path m_routerPath;
    ProfileBuilder::PathStatic m_pathStatic;

    // Project windows we've subscribed to `aboutToClose` (deduped).  May
    // include the primary `m_projectWindow` plus any project that owns a
    // result layer currently bound as a source.  Raw pointers are safe
    // here because we manually remove entries in our handler before any
    // project teardown begins (see `onObservedProjectAboutToClose`).
    QSet<SWMMVisProjectWindow *> m_observedProjects;

    /**
     * @brief Deduplicate a context-bound project-close observer.
     * @param[in] pw Nullable borrowed project retained only as a raw set key while its aboutToClose
     * lifecycle is observed; null/already observed is ignored.
     */
    void subscribeProjectClose(SWMMVisProjectWindow *pw);
    /**
     * @brief Sever this project observer and retire primary context or its secondary source
     * actions.
     * @param[in] pw Nullable borrowed closing project, still alive during aboutToClose. Primary
     * close advances both worker cookies, clears project/canvas/model and closes; secondary close
     * removes descendant actions, updates counts and rebinds remaining sources.
     */
    void onObservedProjectAboutToClose(SWMMVisProjectWindow *pw);

    ProfilePlotWidget *m_plot = nullptr;
    ProfileLayerPanel *m_layerPanel = nullptr;
    QToolButton *m_sourceButton = nullptr;
    QMenu *m_sourceMenu = nullptr;
    QProgressBar *m_loadingBar = nullptr;
    int m_loadCookie = 0; // ignores stale watcher returns
    ProfilePlotOptions *m_options = nullptr;

    // ── Attribute tracks state ──────────────────────────────────────────
    ProfileAttributeTrackOptions *m_trackOptions = nullptr;
    ProfileAttributeTracksWidget *m_tracks = nullptr;
    QScrollArea *m_tracksScroll = nullptr; ///< the splitter pane; scrolls when many tracks
    QWidget *m_plotHolder = nullptr;       ///< carries the scrollbar-matching right gutter
    QSplitter *m_profileSplit = nullptr;
    QMenu *m_tracksMenu = nullptr;
    QAction *m_actShowTracks = nullptr;
    /*! The "Link attributes" section header — species node-scope entries
     *  are inserted above it so each scope's species sit with its fixed
     *  attributes. */
    QAction *m_tracksLinkSection = nullptr;
    /*! Species entries currently in the Tracks menu — rebuilt whenever
     *  the source set (and so the union of run species) changes. */
    QList<QAction *> m_speciesTrackActions;
    int m_trackLoadCookie = 0;
    bool m_syncingX = false;
    QList<int> m_lastSplitSizes; ///< restore target after collapse

    /*! Rebuilds the species entries of the Tracks menu from the union of
     *  species across the current source layers (Y2b-2 follow-up). */
    /**
     * @brief Replace scope-qualified species menu entries with the union from current discoverable
     * 1D source layers.
     * Includes unchecked runs, skips duplicate/empty species, puts node scope before link section
     * and link scope after fixed links; actions edit/persist shared track options.
     */
    void refreshTracksMenuSpecies();

    /*! Per-(layer, variable) sampled-profile cache. The key's string leg
     *  is `"a:<int(PlotAttribute)>"` for fixed attributes and the
     *  scope-qualified species token `"<name>@node"` / `"<name>@link"`
     *  for species tracks. Invalidated by the same signals as
     *  m_sourceCache (see ensureCacheInvalidationWired). */
    QHash<QPair<SWMMResultsLayer *, QString>,
          std::shared_ptr<const ProfileAttributeSampler::AttributeProfile>>
        m_attrCache;
    // Live-run throttle for the attribute-track re-read (appendLivePeriods):
    // the tracks re-fetch their whole period range from the .out, so at most
    // once per 2 s (leading edge; a pending tick re-reads at expiry).
    QTimer m_liveTracksThrottle;
    bool m_liveTracksPending = false;
    bool m_liveTracksWired = false;
    QPointer<SWMMResultsLayer> m_liveTracksLayer;

    // Layer pointer (non-owning) per menu action.
    QHash<QAction *, QPointer<SWMMResultsLayer>> m_actionLayer;

    // Per-layer cache of computed profile data. Populated lazily by
    // rebindSources(); invalidated on `resultsOpened`,
    // `resultsFilePathChanged`, or layer destruction so subsequent UI
    // option toggles reuse the cached `SourceDerived` instead of
    // refetching the .out file every time. `m_cacheWired` tracks the
    // layers we've already connected invalidation signals on to avoid
    // duplicate connections.
    QHash<SWMMResultsLayer *, std::shared_ptr<ProfileBuilder::SourceDerived>> m_sourceCache;
    /*! The fetched series each cached SourceDerived was computed from —
     *  kept so a live layer's new periods can be appended
     *  (ProfileSourceFetcher::appendTail + ProfileBuilder::appendPeriods)
     *  instead of refetching the whole .out on every progress tick. */
    QHash<SWMMResultsLayer *, std::shared_ptr<ProfileBuilder::SourceSeries>> m_sourceSeriesCache;
    /*! Rebinds in flight on the worker thread. While non-zero the cached
     *  SourceDerived may be read off-thread, so live appends (which mutate
     *  it in place) are skipped; the next tick catches up. */
    int m_rebindsInFlight = 0;
    QSet<SWMMResultsLayer *> m_cacheWired;
    /**
     * @brief Remove raw/derived/attribute cache entries for one layer identity.
     * @param[in] layer Nullable borrowed key, compared without dereference; safe for the
     * destruction callback. Null is ignored. Does not rebuild views or remove the wired flag.
     */
    void invalidateSourceCacheFor(SWMMResultsLayer *layer);
    /**
     * @brief Subscribe once to result-open/path/live/finalization/destruction signals.
     * @param[in,out] layer Nullable borrowed layer, required live when first wired. Context-bound
     * callbacks invalidate caches/rebind or append; destruction removes keys/wired identity and
     * advances both cookies to discard in-flight hash results.
     */
    void ensureCacheInvalidationWired(SWMMResultsLayer *layer);

    /*! Live 1D results: extend \p layer's cached series + derived with the
     *  periods it just gained, then repaint the plot and refresh the
     *  attribute tracks. Falls back to a full rebind when nothing is cached
     *  for the layer or the append cannot be applied. */
    /**
     * @brief Append newly readable cached raw/derived periods, or invalidate/rebind when
     * cache/append is unavailable.
     * @param[in,out] layer Nullable borrowed results source; no-op without plot or while any rebind
     * worker may read shared arrays.
     * Refreshes LID/plot/axes and invalidates layer attribute cache on a leading-edge two-second
     * tracks throttle. The latest pending layer is retained through QPointer; worker completion
     * catches skipped final producer tails.
     */
    void appendLivePeriods(SWMMResultsLayer *layer);

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
     * Result is true when at least one source was restyled.
     */
    /**
     * @brief Propagate only plot-level HGL/EGL/envelope pens/brushes changed since last
     * observation.
     * First call seeds the snapshot without writing any layer; later changed fields apply to every
     * listed surviving source, checked or not, preserving untouched per-source customization.
     * @return True when at least one source receives a changed style field, false for seeding/no
     * changes/no options/no sources.
     */
    bool pushEditedPlotStylesToSources();

    /*! Last-seen plot-level styles, so an edit can be told from a repaint.
     *  Seeded from the options object when it is bound. */
    struct PlotStyleSnapshot {
        QPen hglLinePen;
        QBrush hglFillBrush;
        QPen eglLinePen;
        QPen maxHglLinePen;
        QBrush maxHglFillBrush;
        QPen maxEglLinePen;
        bool seeded = false;
    } m_lastPlotStyle;
};

#endif // PROFILE_PLOT_DIALOG_H
