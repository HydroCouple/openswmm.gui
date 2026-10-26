/**
 * @file      swmmvis.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   MIT
 * @brief     Main application window for SWMMVis.
 *
 * @details SWMMVis is a QMainWindow MDI host that coordinates:
 *  - Multiple SWMMVisProjectWindow sub-windows (one per loaded project).
 *  - The shared MapCanvas (inside each project window).
 *  - Dock panels: Layer Tree, Object Browser, Attribute Panel, Attribute Table,
 *    Simulation Status log, and Message Log.
 *  - Toolbars for map tools, animation controls, and editing operations.
 *  - Application menus (File, View, Layer, Simulation, Help, Window).
 *
 * The active project window is the last-focused MDI sub-window; switching
 * sub-windows rebinds all panels to the new project's canvas and selection
 * bus without recreating any widgets.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef SWMMVIS_H
#define SWMMVIS_H

#include <QDateTime> // 2026-07-19 — mPendingSeekTime (scrub coalescing)
#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <QSettings>
#include <QUuid>
#include <functional>
#include <optional>
#include <utility>

#include "core/openswmmvislogmessage.h"
#include "layers/swmmmodellayer.h"      // NewProjectSpec (nested type)
#include "selection/selectionmanager.h" // SWMMObjectRef
#include "plot/plotattribute.h"
#include "plot/resultdescriptor.h" // PlotAttribute (AT.2)
#include "plot/profilerouter.h"    // ProfileRouter::Path

QT_BEGIN_NAMESPACE
namespace Ui
{
class SWMMVis;
}
namespace ProfileSection
{
struct Definition;
}
QT_END_NAMESPACE

namespace openswmmvis::ui
{
class CompactToolbarController;
}
namespace openswmmvis::ui
{
class ComparisonPlotDialog;
class PlotDock;
}
namespace openswmmvis::ui
{
class RainfallVisualizationDialog;
}
namespace openswmmvis::ui
{
class RibbonGroup;
}
namespace openswmmvis::project::examples
{
struct ExampleInfo;
}
namespace openswmmvis::ui
{
struct LayerExportTarget;
}
namespace openswmmvis::io
{
struct ExportReport;
}

class QCheckBox;
class QLabel;
class QToolButton;
class QComboBox;
class QLineEdit;
class QSlider;
class QDoubleSpinBox;
class QDateTimeEdit;
class QProgressBar;
class QTimer; // 2026-07-19 — mScrubCoalesceTimer (scrub coalescing)
class QStandardItemModel;
class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QMimeData;
class OpenSWMMVisLayer; // Slice BI-MK.LT — onLayerKindStyleRequested signature
class QMdiSubWindow;
class QAction;
class QMenu;
class GISRasterLayer;
class OpenSWMMVisWorkspace;
class SpatialReferenceSystem;
class MapCanvas;
class SWMMVisProjectWindow;
class LayerTreePanel;
class ObjectBrowserPanel;
class PropertiesPanel;
namespace openswmmvis::ui
{
class SectionViewPanel;
}
class AttributeTablePanel;
class SimulationStatusModel;
class CheckpointModel;
class CheckpointsDialog;
class ProfilePlotDialog;

namespace openswmmvis::ui
{
class CursorWindowSlider;
}
namespace openswmmvis::ui
{
class LegendDock;
}
namespace openswmmvis::ui
{
class LayerStylingDock;
}
namespace openswmmvis::ui
{
class FeatureLayerPanel;
}
class FeatureLayer;
class SWMMVisProjectWindow;

/*!
 * \class SWMMVis
 * \brief Main application window for SWMMVis.
 *
 * \details Wraps MapCanvas, manages the project workspace/session, map tools,
 *          and all top-level UI interactions (menus, toolbars, status bar,
 *          docks). Each MDI sub-window contains one SWMMVisProjectWindow whose
 *          canvas is rebound to the shared panels on focus change.
 */
class AnimationController;

class SWMMVis : public QMainWindow
{
    Q_OBJECT
    friend class TestExtensionActivity; ///< Native acceptance fixture; does not expose SDK/private slots.

public:
    /**
     * @brief Construct the main MDI host, owned docks/toolbars/actions and initial workspace on
     * the GUI thread.
     * @param parent Optional Qt owner; caller owns a parentless top-level window.
     * Builds the generated UI, installs project drag/drop, restores settings and registers
     * shared actions. Startup environment hooks can queue model opening, figure capture or
     * trace performance runs; the latter can quit the app after its requested runs. Owned
     * children live with the window, while active project/layer references follow rebinding
     * and close signals. Constructor does not synchronously load an ordinary startup model.
     */
    explicit SWMMVis(QWidget *parent = nullptr);

    /**
     * @brief Delete project windows while shared controls still exist, then delete the generated
     * UI wrapper.
     * QObject ownership destroys remaining children. Does not itself call saveSettings;
     * normal accepted closeEvent performs that persistence. Active projects must shut down
     * through their usual lifetime protocol; no automatic save/rollback is added here.
     */
    virtual ~SWMMVis();

    /**
     * @brief Prompt for a target format/path, normalize writable suffixes and save the specified
     * project.
     * @param pw Optional borrowed project; null returns false. Must survive the modal chooser;
     * raw pointer is not locally guarded against project destruction during nested events.
     * @return True after successful saveAs and recent/settings/log updates; false on cancel,
     * empty path or writer refusal (critical dialog). Remembers per-project filter before write,
     * so that preference can change even when save fails. Engine target can differ from chosen
     * .oswp path; writer owns sidecar policy. Preflight warnings do not block. No transactional
     * rollback of partially written files or input ownership transfer is provided here.
     */
    bool saveProjectWindowAs(SWMMVisProjectWindow *pw);

    /**
     * @brief Run synchronous pre-save portability analysis and log its warnings without blocking
     * save.
     * @param pw Optional borrowed project; missing model/live engine is inert.
     * @param targetPath Borrowed intended output path consumed by normalizer.
     * @param isGpkg True chooses GeoPackage preflight, false input-deck preflight.
     * Does not modify/rebase engine paths, write files, return approval or abort the caller;
     * engine writer remains authoritative for actual save-time normalization.
     */
    void logPortabilityPreflight(SWMMVisProjectWindow *pw, const QString &targetPath, bool isGpkg);

public slots:
    /**
     * @brief Append a timestamp, severity name and message to the owned Message Log model.
     * @param message Borrowed text copied into a new row; no reference is retained.
     * @param messageType Valid LogMessageType enum whose meta-enum key exists; unrecognized
     * values are not guarded before conversion to std::string. Default is Information.
     * Requires initialized UI/model and GUI-thread use. Exceeding 20,000 rows trims the oldest
     * 2,000 in one batch. Coalesces scrolling to one queued callback per event-loop turn;
     * does not persist to disk or synchronously force a view relayout for every appended row.
     */
    void onLogMessage(const QString &message,
                      OpenSWMMVisLogMessage::LogMessageType messageType =
                          OpenSWMMVisLogMessage::LogMessageType::Information);

private:
    /**
     * @brief Build the owned welcome content, MDI tab behavior, links, examples and startup
     * preference wiring.
     * Clears prior dynamic content, installs chrome/right-side close style and a nondeleting
     * welcome-subwindow filter. Honors and persists the welcome toggle; external links open
     * through QDesktopServices. Discovers user-seeded examples, groups them by category and
     * captures each ExampleInfo by value for copy/open actions. Repeated use can add duplicate
     * button signal connections; intended for initial construction with generated UI ready.
     */
    void initializeWelcomeScreen();

    /**
     * @brief Choose a destination, copy an example bundle or legacy file there, then open the
     * copy.
     * @param info Borrowed example metadata read synchronously; paths/names determine the
     * subdirectory and open target. Source remains the baseline. Existing destination offers
     * Open Existing, Replace or Cancel; Replace recursively deletes that destination before
     * copying. Copy failure warns and can leave partial output with no rollback. Uses a wait
     * cursor during synchronous copying; cancel/no selection is inert. Does not open baseline.
     */
    void openExampleCopy(const openswmmvis::project::examples::ExampleInfo &info);

    /**
     * @brief Create the owned Analysis/Animation toolbars, then initialize animation, terrain,
     * mesh-editing and active-result selectors.
     * GUI-thread construction only, called once with generated UI ready. Stable object names
     * support persisted window layout; registerActions later mounts bars in the compact
     * controller.
     */
    void initializeToolBars();
    /**
     * @brief Create the owned animation controller and Playback/Timeline/Display ribbon groups
     * once.
     * Requires generated transport actions and Animation bar. Restores/persists speed presets
     * [.25,8] times, enables cyclic playback initially, shows UTC-labeled read-only time, and
     * sets a ten-minute look-back default. Window controls use minutes converted to
     * seconds/milliseconds; single normalized cursor maps to driver time and coalesces scrubs
     * with a 40 ms trailing timer, seeking immediately on release.
     * One live-rendering control follows the Analysis-selected 1D and 2D outputs.
     * Scrubbing pauses that shared clock while incoming history remains available. New
     * controls/controllers remain Qt-owned; data layers stay project-owned.
     */
    void initializeAnimationToolBar();
    /**
     * @brief Create and dock the Qt-owned terrain toolbar and its checkable profile-trace action.
     * Action callbacks resolve the active project at toggle time, returning to Select when
     * unchecked; no project resets the action. This initializes widgets only, with no terrain
     * read until requested; compact toolbar later reparents/mounts the bar.
     */
    void initializeTerrainToolBar();
    /**
     * @brief Create the owned mesh editing toolbar and bind callbacks resolving current
     * project/model/mesh on use.
     * Adds vertex/edge/cell selection, terrain-profile tracing,
     * cell-data/infiltration/groundwater/snow actions and aquifer/state dialogs. Registers
     * picker/lister callbacks for timeseries, curves, node IDs/raw model-CRS coordinates and
     * optional groundwater preview (configured engine mode, seeping conduits and subcatchments).
     * Registries/layers remain project-owned; factories create Qt-owned controls/dialogs, and
     * modal targets must survive their operation.
     * Assignment dialogs use active/current-project meshes, mark dirty and can persist
     * assignment-history recipes; groundwater node undo validates engine and captured mesh
     * revisions and invalidates on engine close. Source callbacks do not own layers. Called once
     * before catalog adoption so actions can be themed/shortcut-bound; initialization itself does
     * not apply assignments.
     */
    void initializeMeshEditingToolBar(); // Slice §V.VB

    /**
     * @brief Create two owned active 1D/2D result selectors in the Analysis Results Layers group.
     * User activation routes stored borrowed layer identities (zero means none) to the current
     * project; widget hydration uses refreshActiveResultsCombos and does not select the first
     * source implicitly. Requires the initialized Analysis bar; called once.
     */
    void initializeAnalysisLayerCombos();

    /**
     * @brief Hydrate active-result selectors under blocked signals from the current project.
     * Uses the 1D statistics registry and 2D canvas layers, stores borrowed pointers as quintptr
     * item data, reselects exact active identities or none and disables empty selectors. Updates
     * selected layer identities without changing playback mode. Without a project disables
     * the selectors.
     */
    void refreshActiveResultsCombos();
    /**
     * @brief Make tool actions checkable and connect them to whichever project is active when
     * triggered.
     * Binds edit-session gating, selection/navigation/profile/report/time-series routes, creates
     * saved-section/rainfall/2D-export actions before catalog registration, and observes MDI
     * project activation. Saved-section picker QPointer-guards its modal project target.
     * Zoom-to-selection unions visible model object extents and pads them (degenerate points use
     * layer-relative buffer); coordinates are used as supplied by those layers. Startup disables
     * existing-feature mutation actions. Does not instantiate every project map tool or mutate
     * geometry itself.
     */
    void initializeMapTools();
    /**
     * @brief Create owned engine/unit/progress/offset/auto-length/coordinate/scale/CRS status
     * controls and routes once.
     * Requires generated status bar, actions and singleton unit service. Project selection later
     * enables engine/offset/auto-length/scale controls; unit changes hydrate the combo with
     * signals blocked. Offset switching applies LINK_OFFSETS before an optional conversion
     * question, then delegates numeric interpretation to the project; declining conversion does
     * not revert the chosen mode. Percent/coordinate/scale readouts remain UI state, while user
     * routes may edit active project options. Does not start simulation or choose a CRS during
     * construction.
     */
    void initializeStatusBar();

    /**
     * @brief Format cached cursor coordinates in model CRS when available, otherwise canvas CRS.
     * Attempts reverse transform without checking a returned success flag, uses seven decimals
     * for geographic or four otherwise, then appends available terrain and mesh elevations side
     * by side (terrain units included when known). Requires coordinate edit; does not enforce
     * mHasCursorPos itself, transform elevations, sample terrain or mutate map/model data.
     */
    void updateCoordinateReadout();
    /**
     * @brief Construct the layer/object/properties/simulation/log/legend/plot dock chain, then
     * log inherited thread settings.
     * Requires generated dock placeholders/UI; called once before final action adoption. Each
     * initializer owns/binds its widgets and no project file is opened here.
     */
    void initializeDockWidgets();
    /**
     * @brief Replace the generated Layers dock placeholder with an owned, initially unbound
     * LayerTreePanel.
     * Context routes open modal live LayerStyleDialog with current canvas undo stack, dispatch
     * attribute tables/exports/kind renderers/time-series, and select active results on the
     * current project. Layer/style targets are borrowed and must survive their modal use; Qt owns
     * new controls/dialogs. Repaints the then-active canvas after style close. The panel is
     * retargeted on project activation rather than owning project layers.
     */
    void initializeLayersDockWidget();
    /**
     * @brief Create the owned Object Browser dock, tabify it with Layers and connect
     * plot/rainfall/table routes.
     * Panel uses subsequent active-project rebinding; data-category requests reveal the shared
     * attribute dock before selecting its category. Intended once after generated UI setup;
     * repeating replaces member pointers and adds children/connections.
     */
    void initializeObjectBrowserDockWidget();
    /**
     * @brief Create owned Properties, Section View, Features and Attribute Table docks and their
     * edit bridges.
     * Tabifies right panels, connects rainfall requests and bidirectional object refresh, updates
     * section graphics and marks the then-active model dirty on adapter edits. Correct active-
     * project rebinding is required so dirty notifications reach the edited model. Intended
     * once; does not itself bind a project, write engine values or save an input file.
     */
    void initializePropertiesPanelDockWidget();
    /**
     * @brief Create the owned status model, Checkpoints action and progress/date delegates on the
     * existing view.
     * Configures stable columns and persistent read-only date editors for new top-level jobs.
     * Context-menu Copy uses tooltip then display text from the captured model index; callers
     * must preserve relevant row during menu execution. Requires generated UI/menus; intended
     * once and does not launch runs or checkpoints.
     */
    void initializeSimulationStatusDockWidget();
    /**
     * @brief Create the owned three-column log model and view-owned Copy/Copy All context
     * actions.
     * Selected rows copy in sorted order without headings; Copy All includes fixed TSV headings.
     * No independent Ctrl+C is registered because main-window focus dispatch owns that shortcut.
     * Requires generated UI; intended once, with actual appending/row cap handled by
     * onLogMessage.
     * Clipboard text is unescaped TSV; embedded tabs/newlines in messages are preserved.
     */
    void initializeMessageLogDockWidget();
    /**
     * @brief Create owned Legend and Layer Styling docks on the right and hide both initially.
     * Stable dock names allow later window-state restoration; repeated calls would create
     * additional docks, so used once during startup.
     */
    void initializeLegendDockWidget(); // Slice BB Phase 8.6.11 / 8.6.16

    // ── Editable feature layers (MESH_DIALOG_TABS_AND_FEATURE_LAYERS_PLAN
    //    §5.1, §6.2) ────────────────────────────────────────────────────
    /**
     * @brief Create the owned Features panel/dock and wire creation, export, messages and
     * active-layer retargeting.
     * Tabifies behind Properties when available. Layer changes retarget the active project's
     * tools, uniquely connect editingChanged and synchronize edit actions. Intended once;
     * repeated calls create extra children/connections rather than reusing an existing dock.
     */
    void initializeFeatureLayerDockWidget();
    /**
     * @brief Create owned feature/session actions and connect lazy per-project map-tool
     * activation.
     * Drawing tools are reused direct project children so in-progress geometry survives tab
     * switches. Activation targets panel's layer; edit latch requests layer.setEditing then
     * reflects actual state. Delete routes to an undo command. Icons are deferred to theme-aware
     * action registration. Intended once after dock construction and before registerActions;
     * repeating adds duplicate actions and signal wiring.
     */
    void initializeFeatureLayerActions();
    /**
     * @brief Derive the feature GeoPackage beside the project's saved model path without creating
     * it.
     * @param pw Optional borrowed project; missing model or empty model-file path gives empty.
     * @return Owned absolute directory plus complete model basename and .features.gpkg suffix.
     * Does not check filesystem existence/writability or use the project sidecar path.
     */
    [[nodiscard]] QString featureGpkgPathFor(SWMMVisProjectWindow *pw) const;
    /**
     * @brief Prompt for a feature schema, create its table in the saved model's feature
     * GeoPackage and add it to canvas.
     * Requires active project/canvas and a nonempty saved model path; missing context informs.
     * Cancel is inert. Ensures GeoPackage, creates layer with chosen geometry/Z/schema/CRS/role,
     * then refreshes/selects Features panel and raises dock. Errors warn; a created container
     * can remain after layer-creation failure, with no rollback. Raw project/canvas references
     * must survive the modal configuration dialog; no local guard or creation undo wrapper.
     */
    void onNewFeatureLayer();
    /**
     * @brief Aim each existing direct-child feature drawing/editing tool at the supplied layer.
     * @param pw Optional borrowed project owning the tools; null is inert.
     * @param layer Optional borrowed feature target passed to
     * point/line/polygon/part/hole/vertex/
     * move tools. Null clears their target. Does not create missing tools, change active tool,
     * validate canvas membership or transfer ownership; tools enforce their lifetime policy.
     */
    void retargetFeatureTools(SWMMVisProjectWindow *pw, FeatureLayer *layer);
    /**
     * @brief Push deterministic sorted feature deletion on the active canvas undo stack.
     * Requires active project/canvas, panel, selected editable-session layer and selected IDs.
     * Empty selection shows four-second guidance. Assumes a nonnull canvas undo stack; command
     * owns mutation/undo and reports lastError, which this handler warns about after push.
     * Success shows requested count. No confirmation, standalone transaction or layer deletion.
     */
    void onDeleteSelectedFeatures();
    /**
     * @brief Reflect the panel's current layer edit session into the latch and eight session-only
     * actions.
     * Blocks latch signals during checked-state reflection; latch enablement requires an
     * editable layer. Seven tools plus Delete follow isEditing. Does not start/stop sessions,
     * activate a tool, clear selection or mutate features.
     */
    void syncFeatureEditState();

    /**
     * @brief Configure and export an explicit layer using a GUI-thread snapshot or copied source
     * paths.
     * @param target Borrowed layer/kind/category selection consumed through modal setup and job
     * creation. Requires active project, valid target/layer; caller must keep the target struct
     * alive for this synchronous call. 2D results delegate to export2DResults.
     * Builds source-kind defaults and protected paths, then guards project/layer/model/run across
     * dialog. Snapshots SWMM/mesh tables on GUI thread; vector/raster jobs carry filenames and
     * selected IDs rather than immutable file bytes, so backing datasets must remain stable.
     * Runs worker via runExportJob, logs warnings/cancel, warns report errors and optionally
     * opens
     * exported vector layers asynchronously on the original surviving canvas. Dialog/exporter
     * own overwrite protection, validation and cleanup; no transaction spans model state/imports.
     */
    void onExportLayerRequested(const openswmmvis::ui::LayerExportTarget &target);
    /*! Run \p job on a QtConcurrent worker behind a modal progress dialog
     *  whose Cancel the job's progress callback reports. */
    /**
     * @brief Run a snapshot-only export callable on QtConcurrent while a nested GUI loop displays
     * progress.
     * @param title Borrowed progress title copied into local modal progress UI.
     * @param job Callable copied to worker; must avoid GUI/engine state and own all captured
     * snapshot/path lifetimes. Receives thread-safe progress callback (false means cancel) and
     * nonnull report output. Progress text argument is ignored; numeric counters cross atomics.
     * @return Report returned by worker; callable's boolean result is deliberately ignored, so
     * job must put any failure/cancellation in report.error. Waits for worker completion even
     * after Cancel; worker must observe callback to stop. Exceptions from watcher.result
     * propagate.
     * Main window must outlive nested work; no independent rollback or forced worker termination.
     */
    [[nodiscard]] openswmmvis::io::ExportReport
    runExportJob(const QString &title,
                 const std::function<bool(const std::function<bool(int, int, const QString &)> &,
                                          openswmmvis::io::ExportReport *)> &job);
    /**
     * @brief Export chosen time steps/maxima from an explicit-or-active 2D source behind modal
     * configuration/progress.
     * @param preselected Optional borrowed results layer; null resolves active project's active
     * 2D layer. Requires active project and source with time samples; missing context
     * warns/informs.
     * Guards layer across configuration and verifies unchanged source before export. Live engine
     * history is pinned and final source adoption deferred until scope exit, then history unpins
     * before queued adoptions swap sources. Appends can continue while history is pinned.
     * Export runs on GUI thread because source is GUI state; callback pumps events and checks
     * Cancel. Caller/UI must prevent nested exports and source/layer destruction during the
     * actual
     * write: global in-flight flag is not nesting-counted and source is not rechecked at every
     * progress callback. Exporter owns cancellation cleanup/atomicity; wrapper logs report and
     * warns failures. Does not change model geometry or save input files.
     */
    void export2DResults(class SWMM2DResultsLayer *preselected);

    /**
     * @brief Wire precreated actions and create project/data/panel/appearance/Window menu entries
     * after required UI, panels and toolbar-born actions exist.
     * @note Intended once on GUI thread: repeated calls duplicate signal connections, menus and
     * owned actions. Connections resolve active project/canvas at trigger time; this setup itself
     * does not run a model or create network objects. Data Objects actions launch existing-entry
     * browse editors rather than implicitly creating providers; unsupported editor categories
     * remain disabled.
     * @note Pause/Stop target the first active selected job in Simulation Status, otherwise the
     * sole active runner; ambiguous multiple runners prompt and no runners log. Pause mirrors
     * requested state, reverting checked flag if unresolved. Stop temporarily pauses for
     * confirmation; Yes unpauses then requests cancellation, No resumes only if it was not
     * already paused. Raw runner lifetime must survive nested confirmation; final output
     * flushing/loading belongs to runner completion code.
     * @note Legend is lazy per-canvas overlay; no active canvas reverts toggle. Set Style uses
     * selected tree layer, otherwise highest-index visible canvas layer, opens modal editor with
     * current undo stack and invalidates active canvas after exit even on rejection. Style
     * Manager is a reused modeless delete-on-close child retargeted from current layer selection.
     * Appearance writes global preferences/theme; dock visibility and actions are bidirectionally
     * synchronized.
     * @note Generation/import/assignment actions check necessary project context and open their
     * own editors; successful mesh-generation dialog acceptance sets path-keyed Module2DEnabled
     * preference. Existing dialog mutation/publication/undo contracts remain with those editors.
     * Climatology solar action deliberately routes to Heat Configuration Solar tab.
     * @note Window Minimize/Zoom target main window, not MDI project tabs. Rebuild dynamic Window
     * entries on MDI activation, dialog-registry membership changes and aboutToShow so retitled
     * dialogs are current. Most new menus/actions are Qt-owned by main window/menu; reused
     * existing actions keep their existing ownership.
     */
    void initializeMenus();
    /**
     * @brief Restore version-two dock state, window state, screen-clamped geometry and recent
     * files.
     * Balances one busy-progress reference around synchronous QSettings reads and recent-list
     * rebuild. Requires generated UI/docks/actions. Uses current values as defaults and ignores
     * restoreState/settings failure; does not open recent models or modify their files.
     */
    void initializeSettings();

    /**
     * @brief Create owned application Undo/Redo, palette and keyboard-preferences actions and
     * adopt catalog matches into the action registry.
     * Called once after UI/actions/docks/toolbars exist on the GUI thread. Installs this app
     * event filter, routes undo/redo to the active project stack, lazily creates a parent-owned
     * palette and opens modal preferences. Registry applies effective shortcuts and themed icons
     * where resolvable; missing catalog actions are tolerated. Finishes compact-toolbar assembly
     * and contextual visibility. Does not take ownership of project stacks.
     */
    void registerActions();

    /**
     * @brief Bind global Undo/Redo enablement to the selected project canvas stack.
     * @param[in] pw Nullable borrowed project, read synchronously; null/missing canvas/stack
     * disables both actions. Disconnects the old enabled-state routes and connects new stack
     * signals with action contexts; stack/project ownership remains external. If either global
     * action is missing, returns before disconnecting old routes.
     */
    void rebindUndoRedoActions(SWMMVisProjectWindow *pw);
    QMetaObject::Connection mUndoEnableConn;
    QMetaObject::Connection mRedoEnableConn;

    /**
     * @brief Assemble owned compact ribbon tabs/groups from existing shared QActions and adopted
     * widget-heavy toolbars.
     * Requires registry/actions, Analysis/Animation/Terrain/Mesh bars initialized; called once.
     * Resolves names tolerantly, assigns shorter iconText captions, builds
     * Home/Model/Features/Mesh/View groups and contextual Terrain/Mesh tabs, finalizes the
     * controller and installs responsive compactors. Shares QAction identities without
     * reparenting them; controller/tab persistence manages bar display. No project/model data is
     * edited.
     */
    void initializeCompactToolbar();

    /**
     * @brief Gate import actions and contextual Terrain/Mesh ribbon tabs from the active project
     * canvas.
     * Shows Mesh for either terrain mesh or 2D results, Terrain for any GIS raster, and leaves
     * Features noncontextual. Disconnects previous canvas add/remove subscriptions and rebinds
     * the new ones to controller context so main-window disconnection sweeps do not remove them.
     * Borrowed canvas lifetime belongs to its project; called on project/layer changes.
     */
    void updateContextualTabs();

    /**
     * @brief Save plot dock layouts, version-two main-window state, geometry and recent file list
     * to QSettings.
     * Skips absent plot docks. Does not save project models, force QSettings synchronization,
     * check write errors or persist modeless editor state directly.
     */
    void saveSettings();
    /**
     * @brief Remove recent-file layout items and queue contained widgets for deletion.
     * Finds the scroll area inside generated recent-files frame; absent content/layout is inert.
     * Deletes layout items immediately, including spacers. Does not clear example/help content,
     * recent-file settings or the preserved welcome subwindow.
     */
    void clearPreviousWelcomeScreenElements();
    /**
     * @brief Lazily create the owned pathless default workspace when none exists.
     * Repeated calls reuse mProject; no project file, model load or lifecycle log is produced.
     */
    void initializeDefaultWorkspaceSession();
    /**
     * @brief Gate existing-feature mutation actions without changing project/model data.
     * @param[in] active Enabled state applied to existing Move Node, Edit Vertex and Delete
     * Selected actions found by object name; absent actions are skipped. Add-feature actions are
     * governed separately by project availability.
     */
    void applyEditSessionToActions(bool active);
    /**
     * @brief Gate network/feature creation actions from project availability.
     * @param[in] open True enables the listed creation/drawing/modification actions, false
     * disables them. Missing objects are skipped. Generate Mesh is deliberately not included
     * here; no tool activation or edit-session state is changed.
     */
    void applyProjectOpenToActions(bool open);

    /**
     * @brief Bold the status label matching the selected link-offset interpretation.
     * @param[in] elevation True bolds Elevation and unbolds Depth, false the reverse; missing
     * labels are ignored. Changes fonts only, not model options/offset values.
     */
    void updateOffsetModeLabels(bool elevation);

private slots:
    /**
     * @brief Select a kind renderer class when requested, then run its scoped live style dialog.
     * @param layer Borrowed model/results layer; other/null types are ignored. Must survive
     * modal editing; no local QPointer protects raw target through reentry.
     * @param kindOrdinal Model Category in [0, NumCategories); other ordinals are ignored.
     * @param rendererId Borrowed class ID. Same class preserves tuning; single resets defaults,
     * graduated installs five equal-interval Viridis bins with kind-specific attribute, and
     * categorized uses tag. Empty/unknown IDs retain renderer and still open scoped dialog.
     * Renderer replacement occurs before the modal editor and is not rolled back on cancel by
     * this dispatcher. It invalidates the then-active canvas on replacement and after exit,
     * and passes that canvas's optional undo stack to the style dialog.
     */
    void onLayerKindStyleRequested(OpenSWMMVisLayer *layer, int kindOrdinal,
                                   const QString &rendererId);

    /**
     * @brief Open a modal type/object/variable picker against a particular output layer, then
     * plot its selection.
     * @param layer Optional borrowed results layer; null is inert. Attempts openResults when
     * its output handle is absent and warns on failure. Layer/handle must remain valid during
     * the modal picker; this raw argument is not guarded against deletion during event reentry.
     * Enumerates node/link/subcatchment IDs, fixed attributes and named species (species labels
     * use US units here). Cancel or empty selection is inert. Descriptor dispatch retains the
     * explicit layer instead of choosing another project's active run.
     */
    void onPlotTimeSeriesFromOutputLayer(class SWMMResultsLayer *layer);

    /**
     * @brief Create an in-memory untitled model from persisted simulation/2D defaults with
     * today's 24-hour interval.
     * Uses configured default engine (falling back to current engine version) to set engine-aware
     * options. Retains automatic thread count when configured. Delegates blank construction;
     * no template/CRS picker, input-file write, recent-file update or sidecar discovery here.
     */
    void onNewProject();

    /**
     * @brief Create/wire an untitled project window and synchronously initialize its blank
     * engine.
     * @param spec Borrowed model/default/date specification consumed by initializeBlankModel.
     * Logs warnings; failure logs errors, shows critical feedback and requests window close.
     * Success maximizes/activates it, forces shared-panel rebinding and logs in-memory status.
     * Does not save files, register recents, hydrate sidecars or discover existing results.
     */
    void openUntitledProject(const SWMMModelLayer::NewProjectSpec &spec);

    /**
     * @brief Allocate/register a project MDI window and connect shared lifecycle, status,
     * save-warning and title routing.
     * @param filePath Borrowed initial model path copied by project constructor; empty supports
     * untitled.
     * @param instanceId Optional persistent model identity restored before session registration.
     * @param projectPath Optional selected project sidecar; empty infers the input's sibling.
     * @return Borrowed MDI-owned project window, not loaded or shown here. Requires initialized
     * workspace/UI/docks. Close cancels its plot picker, removes plot sources/status and rebinds
     * to a surviving project before teardown. Queued save warnings log all entries and show a
     * modal for the data-loss phrase. Destruction clears cached active pointer; callers must
     * not retain the raw result across unguarded project close or nested event processing.
     */
    SWMMVisProjectWindow *createProjectWindow(const QString &filePath, const QUuid &instanceId = {},
                                              const QString &projectPath = {});

    /**
     * @brief Open a supplied path or prompt with plugin input/project filters, then dispatch by
     * extension.
     * @param path Borrowed optional path; empty opens the remembered model picker. Cancel/empty
     * is inert. Nonempty paths seed picker history even for later load failure. Case-insensitive
     * .oswp uses project JSON dispatch; every other suffix uses openSingleINP/plugin model load.
     * Copies the argument before any modal work; no file existence validation in this dispatcher.
     */
    void onOpenProject(const QString &path = QString());

    /**
     * @brief Start nonblocking model loading unless the path is empty or an equal-path model is
     * already open.
     * @param filePath Borrowed input path copied into async completion; QFileInfo equality reuses
     * an existing nonclosing model window and activates it. No explicit existence/type precheck here.
     * @param projectPath Explicit selected project sidecar; empty uses the input's sibling.
     * @param sessionIndex Entry in the selected project's sessions array; direct input opens
     * match the corresponding stored input before seeding identity and restoring state.
     * Creates/wires a project, acquires busy/open progress, and connects one-shot load completion
     * to finalizeSingleINPOpen. Worker owns engine parsing; success/failure and sidecar/results
     * adoption occur later. Main-window/project load protocol must keep raw completion target
     * valid; this method does not synchronously wait or return a success result.
     */
    void openSingleINP(const QString &filePath, const QString &projectPath = {},
                       int sessionIndex = 0);

    /**
     * @brief Finish model loading on the GUI thread, then restore GUI state, discover results and
     * queue mesh adoption.
     * @param window Borrowed load target, required live even on failure (close is called).
     * Success
     * requires its model/canvas for downstream restoration; no ownership transfer.
     * @param filePath Borrowed original model path used for recents, sibling files and captured
     * work.
     * @param ok Worker outcome; false retires progress, logs outcome and requests window close.
     * @param warnings Borrowed diagnostics copied into Message Log before outcome summary.
     * @param errors Borrowed errors joined for failure detail; not retained.
     * @param elapsedMs Caller-measured worker/load duration in milliseconds, passed to summary.
     * @param progress Optional borrowed open-progress model; guarded when captured for later
     * work.
     * Success schedules settings write, applies sibling sidecar or optionally creates one,
     * opens the first existing matching sibling output when none restored, then asynchronously
     * loads mesh/prior 2D results. Failed sidecar/output work warns without rolling back model.
     * Reactivates/rebinds the project and then completes the extension readiness barrier.
     * Optional asynchronous GIS/mesh resources may still be loading. May schedule opt-in
     * simulation-options capture. These
     * callbacks assume main-window lifetime and some captured project lifetimes; no overall
     * transaction, cancellation rollback or guaranteed absence of file writes on open.
     */
    void finalizeSingleINPOpen(SWMMVisProjectWindow *window, const QString &filePath, bool ok,
                               const QList<QString> &warnings, const QList<QString> &errors,
                               qint64 elapsedMs, class OpenProgressModel *progress = nullptr);

    /**
     * @brief Parse/build a model's 2D mesh on a worker, then adopt it on the GUI thread if its
     * project survives.
     * @param window Optional borrowed project; missing canvas/model retires all supplied
     * progress.
     * Later adoption uses QPointer; closed/missing target deletes the built unparented layer.
     * @param filePath Borrowed input path copied into worker; no engine handle is read
     * off-thread.
     * @param progress Optional borrowed model, guarded in asynchronous callbacks and used for
     * parse/
     * scene stages. No ownership transfer; main window owns its own watcher/busy bookkeeping.
     * Worker reads inline/external mesh, BCs and units, builds coarse geometry, then moves the
     * layer to GUI thread. Completion logs warnings/standard exceptions, attaches pristine mesh
     * without undo, copies model CRS and starts deferred heavy geometry. No mesh still permits
     * prior HDF5 loading. Opt-in environment hooks can capture UI or trigger simulation later.
     * Main window/application must survive outstanding worker note callbacks. No user
     * cancellation
     * or all-or-nothing adoption; small already-complete meshes retire the stage explicitly,
     * while busy-reference release otherwise follows sceneGeometryReady signaling.
     */
    void attachMesh2DLayersAsync(SWMMVisProjectWindow *window, const QString &filePath,
                                 class OpenProgressModel *progress = nullptr);

    /**
     * @brief Synchronously reopen a prior HDF5 run, restore display policy and attach its owned
     * result source.
     * @param window Required borrowed live project with canvas for successful layer adoption.
     * @param filePath Borrowed model path scanned for OUTPUT_FILE and DRY_DEPTH; relative
     * resolution
     * belongs to SimulationRunner parsers. Engine supplies time anchor/dry depth when available.
     * @param h5Override Borrowed explicit path; nonempty takes precedence and reuses an equal
     * absolute-path HDF5 layer. Empty resolves model path then first existing pending sidecar
     * path.
     * Missing/unopenable files log instead of throwing a UI prompt. New source/layer inherit CRS
     * and legacy coordinate fallback, scan depth frames synchronously for peak/98th-percentile
     * ramp, then restore matching sidecar settings so they win. Explicit add makes layer active
     * and preserves pending restore; automatic load consumes pending restore and only fills empty
     * active slot. Animation fallback is set when no primary exists. Does not run simulation,
     * write results, load every pending run or canonicalize symlinks.
     */
    void maybeLoad2DResults(SWMMVisProjectWindow *window, const QString &filePath,
                            const QString &h5Override = QString());

    /**
     * @brief Log an opening line, show a status message and acquire one nested busy-progress
     * reference.
     * @param path Borrowed path used only for its filename in messages; no file is opened here.
     * Pair with endFileOpen for balanced progress ownership; simulation/open progress can outrank
     * this busy request. UI/model must already exist; no success state or timer is retained.
     */
    void beginFileOpen(const QString &path);

    /**
     * @brief Release one busy reference, clear status text and log the operation's outcome.
     * @param path Borrowed path whose filename identifies the message.
     * @param ok True logs Information with summary/timing; false logs Error with optional detail.
     * @param summary Borrowed success descriptor, omitted from success formatting when empty.
     * @param elapsedMs Caller-measured milliseconds, reported verbatim only on success.
     * @param errorDetail Borrowed failure detail, appended only for a failed operation.
     * Does not close files, cancel workers or own a progress model. Requires a matching begin
     * for correct busy nesting; unconditional status clearing can replace another status message.
     */
    void endFileOpen(const QString &path, bool ok, const QString &summary, qint64 elapsedMs,
                     const QString &errorDetail = QString());

    /**
     * @brief Read project JSON and dispatch each modern session/legacy layer path to asynchronous
     * model opening.
     * @param oswpPath Borrowed project path; read/parse failure logs Error and returns. Valid
     * JSON
     * updates recents/settings before model loads, even when it names no valid sessions. Collects
     * modern sessions[].inpPath, or legacy layers[].path when sessions are absent, resolves
     * paths relative to the chosen project and preserves each session's original index and
     * identity through openSingleINP. Does not synchronously aggregate
     * outcomes
     * or roll back already-opened models if a later entry fails.
     */
    void openProjectFile(const QString &oswpPath);

    /**
     * @brief Open a recent QAction's nonempty path data through the shared project dispatcher.
     * @param action Optional borrowed triggered action; null/empty data (separator/Clear) is
     * inert.
     * Copies the path before dispatch; does not infer action from sender or retain the pointer.
     */
    void onOpenRecentFile(QAction *action);

    /**
     * @brief Save the active model at its current path, or invoke Save As for an
     * untitled/pathless project.
     * Missing project logs warning. Existing-path save logs portability warnings first; failure
     * shows critical feedback and logs warning, success logs model path. Project window owns
     * writer/sidecar synchronization. No local rollback or asynchronous save is introduced.
     */
    void onSaveProject();

    /**
     * @brief Invoke Save As for the active project or log a warning when none is active.
     * Delegates prompt, path normalization, writing and recent/settings updates; discards its
     * boolean result. Does not itself close the project or undo modifications on cancel.
     */
    void onSaveAs();

    /**
     * @brief Prompt for an export path and save the active canvas capture when its suffix is PNG.
     * Missing project/canvas warns; cancel is inert. Capture failure warns and write failure
     * shows critical feedback. Other advertised suffixes explicitly inform that export is not
     * implemented. PNG bytes are forced; no model mutation or project save. Raw active project
     * must survive modal chooser because it is reused after return without a local guard.
     */
    void onExportMap();

    /**
     * @brief Clear in-memory recent paths, rebuild menus/welcome buttons and persist settings.
     * Does not delete files, close projects or clear their separate per-project preferences.
     */
    void onClearRecentFiles();

    /**
     * @brief Truncate cached recent-file list to 20 and rebuild Open Recent menu and surviving
     * Welcome-page shortcuts from that order.
     * @note Removes tail only: no deduplication, existence/readability checks, sorting or
     * settings persistence here. Menu labels use file name, tooltip/data use QFileInfo absolute
     * path; Welcome labels include absolute directory but callback captures original stored path.
     * Enable menu/Clear only for nonempty list; Clear menu action has empty data so generic open
     * dispatcher ignores it.
     * @note Add separator before Welcome Clear button only when absent, remove all old
     * scroll-layout items (widgets deleteLater), then recreate buttons/trailing stretch. Requires
     * initialized menu and the expected QVBoxLayout for Welcome content; missing optional Welcome
     * controls are skipped. Qt owns recreated actions/widgets. No source file reads/deletes or
     * automatic open.
     */
    void onRecentFilesSizeChanged();

    /**
     * @brief Show the preserved welcome subwindow/tab and make it active when still present.
     * No-op without MDI area/matching subwindow. Does not rebuild content, open projects or
     * change the persisted show-on-startup preference.
     */
    void onShowWelcomeScreen();

    /**
     * @brief Perform no operation in this currently empty slot.
     * Does not close the active project or main window, prompt, save settings or cancel jobs;
     * actual close handling is provided by project/main-window closeEvent paths.
     */
    void onClose();

    /**
     * @brief Acquire or release one nested indeterminate-progress request and recompute shared
     * bar state.
     * @param busy True increments busy reference count; false decrements with floor zero.
     * Simulation and measured opens take precedence. Does not launch work, own a file-open
     * progress model or guarantee that every unmatched busy request is eventually released.
     */
    void onSetProgressBarBusy(bool busy);

    /**
     * @brief Run a local parented About dialog modally without changing project context.
     * Dialog owns its transient children; no retained dialog pointer, engine mutation or save.
     */
    void onAbout();

    /**
     * @brief Refresh the active Object Browser after a model-load signal and seed that project's
     * simulation dates.
     * Requires a nonnull sender whose parent is the project window; direct invocation without
     * such a sender is unsupported. Browser refresh occurs only for cached active project;
     * date refresh handles missing/inactive project context. Does not zoom or log open summary.
     */
    void onModelLoaded();

    /**
     * @brief Log an Error entry and show a modal Model Load Error warning.
     * @param msg Borrowed message copied to model/dialog; reference must remain valid through
     * synchronous use. Does not close a failed project, retry parsing or own diagnostics.
     */
    void onModelLoadError(const QString &msg);

    /**
     * @brief Apply selected flow-unit item data to the active project's unit system and mark
     * project changed.
     * @param comboIndex Negative is ignored; nonnegative indexes are converted without explicit
     * upper-bound/enum validation here. Requires initialized combo and active project/unit
     * system;
     * engine pointer may be null if model absent. UnitSystem owns engine update/notifications.
     * No undo wrapper, save or independent conversion loop is performed by this handler.
     */
    void onFlowUnitsChanged(int comboIndex);

    /**
     * @brief Cache the latest scene/canvas cursor position and rebuild the status coordinate
     * readout.
     * @param mapX Canvas CRS horizontal coordinate stored verbatim, without finite validation.
     * @param mapY Canvas CRS vertical coordinate stored verbatim, without finite validation.
     * Requires initialized coordinate widget; readout helper attempts model-CRS transformation.
     * Does not move cursor, alter map extent or retain external references.
     */
    void onCursorPositionChanged(double mapX, double mapY);

    /**
     * @brief Cache an optional mesh-probe elevation and refresh coordinates when cursor position
     * exists.
     * @param z Mesh elevation stored verbatim when finite flag is true; no independent isfinite
     * check.
     * @param finite False clears cached elevation, true stores z. Equal optional value is inert;
     * NaN compares unequal and can repeatedly refresh. No model geometry or terrain mutation.
     */
    void onMeshHoverElevation(double z, bool finite);

    /**
     * @brief Refresh the CRS status button from current project/canvas state.
     * @param srs Borrowed notification pointer, deliberately ignored in the definition. No
     * ownership transfer or retained reference; does not assign/reproject a CRS itself.
     */
    void onCanvasSRSChanged(SpatialReferenceSystem *srs);
    /**
     * @brief Show project authority or fallback view authority, annotating when their authority
     * strings differ.
     * Without either displays Unknown. Requires initialized CRS button; compares authority text
     * rather than full spatial-reference equivalence. Does not change CRS or refresh coordinates.
     */
    void refreshCrsStatus();

    /**
     * @brief Prompt for a CRS, then assign, display-only swap or reproject according to project
     * state/choice.
     * No canvas/cancel/empty CRS is inert. Selected object is locally owned until adopted; no
     * model only updates owned canvas CRS. Provisional projects assign metadata directly. Same
     * nonlocal authority rebinds canvas; other established projects prompt for choice. Reproject
     * requires nonlocal source, transforms engine coordinates, copies CRS into model/canvas,
     * reloads geometry, invalidates raster/scene, fits extent without extra undo and marks dirty.
     * Failure warns without wrapper rollback; this branch does not push a separate reprojection
     * undo command. Captured raw canvas/project/model must survive nested pickers; no local
     * guards.
     * Assignment delegates its own persistence/undo policy to project.assignProjectCrs.
     */
    void onCRSButtonClicked();

    /**
     * @brief Parse positive scale text and set active canvas denominator, or restore its display
     * on invalid input.
     * @param text Borrowed scale string copied by parser; grouping syntax follows parseScaleText.
     * No canvas is inert. Canvas scaleChanged normally canonicalizes display; this handler does
     * not clamp to presets, change model units or persist scale directly.
     */
    void onMapScaleEntered(const QString &text);

    /**
     * @brief Format a canvas denominator into the status combo while blocking its signals.
     * @param denominator Display scale value; formatScaleText owns invalid-value fallback and
     * requires representable rounded positive values. No-op without combo; no map/model mutation.
     */
    void onCanvasScaleChanged(double denominator);

    /**
     * @brief Prompt for a local basemap and add the created layer to the canvas active after
     * modal exit.
     * No initial canvas informs; cancel/failed layer creation is inert. Unlike guarded dataset
     * batches this does not pin the original project. Created parentless layer is adopted only
     * when a canvas still exists; disappearance leaves it unadopted here. Logs created name;
     * layer factory owns parsing/network validation and no input-file save is performed.
     */
    void onAddBasemapLayer();

    /**
     * @brief Prompt on WMS/WMTS tab and add the resulting layer to the then-active canvas.
     * Initial missing canvas informs; cancel/null creation is inert. Project can switch during
     * modal UI; no original-target guard, and loss of all canvases leaves a created parentless
     * layer unadopted. Logs layer name; does not synchronously promise tile availability.
     */
    void onAddWMSLayer();

    /**
     * @brief Prompt on WFS tab with an optional geographic extent, then add its created vector
     * layer.
     * Requires initial canvas or informs. Transforms only two opposite extent corners to WGS84;
     * failed/local transform omits the request bound, not a densified nonlinear envelope.
     * After Accepted re-resolves active canvas and creates the layer parented there; cancel/
     * missing canvas/null layer is inert. No original-project pinning or input-file write.
     */
    void onAddWFSLayer();

    /**
     * @brief Queue selected vector datasets in order onto the canvas captured before the chooser.
     * Guards window/canvas through modal selection. Enumerates sublayers off-thread, optionally
     * prompts for a subset, then opens chosen sublayers concurrently and advances batch only
     * after all complete. Missing target discards layers; successful additions request undo and
     * fit extent. Missing CRS warns; failed files do not roll back earlier additions. Worker
     * result exceptions are not caught here; main-window lifetime must cover callback protocol.
     */
    void onAddVectorLayer();

    /**
     * @brief Queue selected raster opens onto the originally captured guarded canvas.
     * Worker opens/statistics, GUI completion adds layer with undo requested and fits extent;
     * failure/target loss logs and deletes unadopted layer. Overview-build signals manage
     * separate
     * busy/log feedback. Files advance sequentially after completion; overview work can continue.
     * No batch rollback, retarget on tab switch or input-file save.
     */
    void onAddRasterLayer();

    /**
     * @brief Queue selected 1D output opens on the original guarded model project.
     * Requires project/model or warns. Reuses equal absolute-path layers, otherwise adds a new
     * model-owned layer before asynchronous open. Success stretches ramp, makes it active and
     * sets global animation primary even if another project became active; failure logs and
     * leaves created unopened layer on canvas. Sequential batch uses first captured error;
     * no multi-file rollback or input save, and completion depends on layer signal protocol.
     */
    void onAddSWMMResultsLayer();

    /**
     * @brief Prompt for one mesh path and start active project's asynchronous mesh import.
     * Requires initial project/model/canvas or warns; cancel is inert. Raw project must survive
     * chooser because it is reused without local guard. Balances two busy references through
     * one-shot meshImportFinished feedback; project owns replacement/adoption/dirty semantics.
     * No mesh parse, direct undo step or source-file write in this wrapper.
     */
    void onAddMesh2DLayer();

    /**
     * @brief Queue explicit HDF5 result paths on the model project captured before the chooser.
     * Guards project/window, keeps original model path for option scanning and reuses the common
     * synchronous 2D loader per queued turn. A matching already-loaded source counts as success;
     * otherwise layer-count/path check determines feedback. Each path advances even after
     * failure;
     * no rollback, simulation launch or automatic project save.
     */
    void onAdd2DResultsLayer();

    /**
     * @brief Open a guarded active project's live Simulation Options and mark changed when it
     * reports writes.
     * Missing model/engine logs warning. After modal exit a surviving project with writes is
     * marked changed and passed to activation handler; its same-project guard may avoid a full
     * refresh. Dialog owns invalidation/apply semantics; rejection is not assumed to undo prior
     * Apply. No direct file save or additional undo wrapper in this entry point.
     */
    void onSimulationOptions();

    /**
     * @brief Edit climatology with engine invalidation and an optional configuration snapshot
     * undo entry.
     * @param tab Requested dialog tab integer forwarded to its own selection validation.
     * Requires guarded active project/model/engine; missing context warns. Captures native
     * configuration and project anchor before modal work, wires engine/model destruction, and
     * on Accepted plus reported writes marks dirty. Pushes undo only when same engine/canvas
     * survives; editor owns validation/application. No file save or rollback on arbitrary exit.
     */
    void onClimatology(int tab);

    /**
     * @brief Open the guarded project's Water Age Sources editor with engine/project-close
     * invalidation.
     * Missing model/engine warns. Apply notifications mark project changed immediately; reported
     * writes mark it again after modal exit regardless of Accepted/Rejected. No extra undo,
     * file save or cancellation rollback is supplied by this wrapper.
     */
    void onEditWaterAgeSources();

    /**
     * @brief Run Initial Quality editor against a guarded project's engine and refresh shared
     * table after reported writes.
     * Requires model/engine or logs warning. Apply marks project dirty; engine/model/project
     * closure invalidates editor. After any modal exit with writes, refreshes Attribute Table
     * even if project disappeared. Editor owns validation/apply; no wrapper undo or file save.
     */
    void onEditInitialQuality();

    /**
     * @brief Run the guarded project's Reaction System editor and mark dirty after any reported
     * writes.
     * Missing project/model/engine logs warning. Wires engine/model/project close invalidation;
     * post-modal dirty update checks guarded project. Does not add undo, save input files or
     * assume that rejection rolled back previously applied edits.
     */
    void onEditReactionSystem();

    //! G4g: [HEAT_SOURCES] / [HEAT_FLUXES] / radiative-solar-cloud editor.
    //! \param tab HeatConfigDialog::Tab to open on; -1 keeps the default.
    /**
     * @brief Edit heat configuration with live dirty notifications, snapshots and follow-on
     * thermal editors.
     * @param tab Nonnegative requests initial HeatConfig tab; negative retains dialog default.
     * Requires guarded active project/engine and wires invalidation before modal execution.
     * Reported writes can push before/after HeatConfigurationUndo only for same surviving
     * engine/canvas/stack, and emit heat option changes; undo refusal is logged. Groundwater
     * request opens thermal-only options, tracks possible partial writes, validates surviving
     * engine and can push configuration undo. Radiation then routes through current active
     * project's climatology, while raster assignment requires original matching engine. No
     * single transaction spans these dialogs, and cancellation does not promise to undo Apply.
     */
    void onEditHeatConfig(int tab = -1);

    // ── Toolbar quick-wins (Phase 2) ────────────────────────────────────────
    /**
     * @brief Reveal the Object Browser's containing dock and focus its search control when panel
     * exists.
     * No project/model mutation or new dock creation; missing panel is inert.
     */
    void onSearch();
    /**
     * @brief Reveal the existing Attribute Table dock and focus its panel with shortcut focus
     * reason.
     * Missing panel is inert; does not change table category, current selection or project data.
     */
    void onTabularView();
    /**
     * @brief Prompt for delimited files and queue sequential loads onto the canvas active when
     * prompt began.
     * Guards main window and canvas through chooser; later tab switches do not retarget batch.
     * Each load is synchronous within its queued turn, balances open feedback, deletes refused
     * layer, or adds it with undo requested and zooms full extent. Cancel/target loss is inert;
     * failures log and later files continue. Does not create a multi-file rollback transaction.
     */
    void onAddDelimitedData();
    /**
     * @brief Open a new modeless delete-on-close statistics dashboard for active project's active
     * 1D results.
     * Missing project warns; no active result informs. Dashboard receives model, selection and
     * canvas context. Does not reuse an existing dashboard, choose first available run, load an
     * output handle itself or transfer project ownership.
     */
    void onSummarizeResults();

    /**
     * @brief Invoke the common 2D export flow with the active project's active 2D result layer.
     * No source selection dialog or worker is added by this wrapper; export2DResults owns checks,
     * live-history pinning, cancellation UI and completion feedback.
     */
    void onExport2DResults();
    /**
     * @brief Dispatch Copy by remembered visible content focus, falling back to current focus.
     * Attribute Table ancestry copies selected rows; Message Log ancestry triggers selected-log
     * copy; otherwise captures active map and writes clipboard pixmap. Missing map/capture
     * failure
     * logs warning. Does not copy arbitrary text editor selection or modify model data; UI focus
     * cache is maintained separately by eventFilter.
     */
    void onCopyActiveView();
    /**
     * @brief Prompt for a printer and print a captured active map, scaled to fit the viewport
     * with aspect preserved.
     * Missing project/canvas warns; cancel is inert and capture failure warns. Uses
     * high-resolution
     * printer and viewport top-left anchor, without centering or explicit painter/printer error
     * check. Raw project must survive print dialog reentry; no local lifetime guard or model
     * save.
     */
    void onPrintActiveView();
    /**
     * @brief Replace active project selection with SelectionOps' inverse and log resulting count.
     * Requires model/live engine/selection manager or warns. Does not mutate network objects,
     * add an undo step, include arbitrary feature-layer IDs or preserve existing selection mode.
     */
    void onInvertSelection();

    // ── Network analysis (Phase 3) ──────────────────────────────────────────
    /**
     * @brief Invoke the common network selection trace with upstream traversal.
     * SelectionOps and onStreamSelect own seed validation, replacement and feedback.
     */
    void onSelectUpstream();
    /**
     * @brief Invoke the common network selection trace with downstream traversal.
     * Does not change hydraulic flow direction or mutate model objects.
     */
    void onSelectDownstream();
    /**
     * @brief Trace the selected SWMM seeds and replace selection with the requested network
     * reach.
     * @param upstream True traces upstream, false downstream through SelectionOps. Missing
     * project/model/engine/selection warns; no seeds logs guidance without changing selection.
     * Success logs traced node/link/subcatchment counts; does not run a simulation or change
     * data.
     */
    void onStreamSelect(bool upstream);
    /**
     * @brief Open/reuse project trace analysis in flow-balance mode using selected result layer
     * when available.
     * @param upstream True requests upstream boundary, false downstream. No active project is
     * inert.
     * Selected tree layer is cast to results or null; TraceAnalysisDialog owns fallback/context
     * validation and async work. No network selection replacement or direct simulation start.
     */
    void onFlowBalance(bool upstream);
    /**
     * @brief Open/reuse project trace analysis in travel-time mode with optional selected output
     * layer.
     * @param upstream True requests upstream trace, false downstream. No project is inert.
     * Dialog/controller owns source fallback, validation and calculations; no ownership transfer.
     */
    void onTravelTime(bool upstream);

    /**
     * @brief Run the active model's User Flags manager and mark project dirty when it reports
     * applied writes.
     * Missing model/engine warns. Apply may persist before later Cancel, so final dirty check is
     * independent of Accepted/Rejected. Raw project/model must survive nested modal execution;
     * this wrapper supplies no local QPointer or project-close invalidation, undo or file save.
     */
    void onUserFlags();

    /**
     * @brief Prepare and launch a disk-backed run for active project's selected engine, wiring
     * per-job status, live results and final-result adoption.
     * @note GUI thread only; requires active project/model/editing engine and nonempty model
     * path. Refuse active checkpoint run or same project/engine status job already running. Exact
     * engine-version 6.0.0 with authored surface ownership redirects to Checkpoints. Dirty
     * project must save successfully before running; pathless projects only log Save first, no
     * chooser here.
     * @note Capture QPointer owner/model plus editing-engine identity, path and edit revision
     * around nested preparation prompts; recheck on 2D warning and path validation after
     * overwrite confirmation. Rebase revision after intentional saves. These guards protect
     * preparation, not every later callback: live/final-result callbacks test guarded
     * project/source existence rather than freezing original edit revision or active tab. No
     * ownership transfer of editing engine; runner owns separate worker engine/process.
     * @note GUI path-keyed Module2DEnabled controls missing-mesh warning and default HDF5
     * setting, not solver activation itself. Mesh probe is heuristic twoDMeshResolves. If enabled
     * mesh resolves and OUTPUT_FILE absent, attempt setting relative &lt;stem&gt;.2d.h5 and
     * saving;
     * failed save aborts with setting left dirty, failed option setter simply leaves default
     * unset. HDF5 overwrite detection also runs when GUI module flag is false.
     * @note Resolve report/binary paths through project settings. Validate predicted
     * report/out/runlog/LID/HDF5/interface destinations and optional legacy compatibility deck
     * against same-file aliases, project model/sidecars/mesh inputs, checkpoint
     * files/reservations, active runner inputs/outputs and RunPathLease paths before closing
     * handles. Revalidate after overwrite prompt; this GUI check itself does not reserve paths.
     * Runner.start repeats native checks and acquires input/output leases before dispatch, so
     * launch can still fail after GUI preparation.
     * @note Prompt only when nonempty prior .out/.h5 or matching loaded results exist, scanning
     * all project canvases. Accepted overwrite later closes matching 1D handles in place and
     * permanently removes/deletes matching HDF5-backed 2D layers without undo, including
     * comparison layers on other projects. Does not itself delete HDF5 file here. These view
     * changes are not rolled back if runner preflight later fails.
     * @note Version prefix 5. writes a complete compatibility input atomically beside report
     * before runner launch: in-tree LEGACY_SWMM_VERSION uses extended SWMM5 profile, other 5.x
     * modes stock profile. Serialization substitutions/warnings logged; canonical project INP
     * preserved. Compatibility preparation failure finishes status job and clears progress.
     * Native versus legacy cancellation/pause semantics remain SimulationRunner's contract;
     * legacy pause flag does not park external process and cancellation may lack normal output
     * finalization.
     * @note Preserve trace analysis before output overwrite; failure aborts. Begin stats-registry
     * run with topology snapshot and mark project dirty before allocating parent-owned
     * SimulationRunner. Runner is retained in active map until finished then deleteLater; on
     * macOS activity assertion spans registered run. Progress/warnings/dates update status and
     * minimum-running-progress bar, pause/cancel enabled until last runner completes.
     * @note Per-tick live 1D ingestion keeps history available independently of display following. On
     * finish finalize registry, classify !success with errorCode 0 as cancellation, and
     * load/finalize 1D only for success/cancel with nonempty .out; genuine error does not
     * auto-open arbitrary partial file. Existing live tail without usable outcome is closed.
     * Successful read auto-stretches; existing Analysis selections and background tabs are
     * preserved. A success with empty .out logs error but
     * does not rewrite runner status to failure.
     * @note 2D initialization consumes copied SI metre geometry and flat four-slot connectivity
     * (v3=-1 triangles), creates or reuses layer by absolute snoopy_h5_path (empty paths can
     * match), installs uniquely owned EngineMesh2DSource, applies model CRS/fallback coordinate
     * scale and positive engine dry depth, and selects it. Live source has preference frame/byte
     * caps. Layer cached as guarded per-job pointer; no raw solver pointers cross worker
     * boundary.
     * @note Live depth/flux/signed vertex/head/rainfall/process bundles pair by elapsed seconds
     * in source; history collects even when hidden, repaint/range refresh mostly only visible.
     * Edge geometry primes velocity reconstruction, envelopes keep latest maxima rather than
     * per-frame history. Rejected process metadata logs and skips frame. No GUI numeric unit
     * conversion of solver SI arrays in these callbacks.
     * @note Finished 2D source is marked non-live first, including genuine failures. If
     * synchronous export is reading it through an event-pumping loop, park finish status in
     * mDeferred2DFinish; exporter drains later, preventing source destruction during read.
     * Otherwise call adoptFinished2DResults, which can synchronously scan HDF5. No synchronous
     * result return or single transaction encompassing saves, compatibility/trace artifacts, view
     * changes and execution.
     */
    void onRunSimulation();

    /**
     * @brief Present variable picker for active 1D output and selected network objects, then
     * batch-add checked series to shared comparison plot and reveal dock.
     * @note Missing project or active 1D results informs and returns; no automatic source file
     * chooser/run. Only selected Node/Link/Subcatchment refs become feature groups, sorted by
     * enum kind then case-sensitive name; rain gages/mesh/GIS refs omitted. System-variable
     * choices remain available without feature selection. Availability/units derive from chosen
     * output via stack-local probe, not global active-project UnitSystem.
     * @note Cancel/empty choices no-op; more than 500 checked entries requires default-No
     * confirmation. Ensure comparison source for captured layer, convert entries to descriptors
     * and submit one batch; downstream handles read/schema failures. Raw project/result/probe
     * context must remain live through modal picker/confirmation: no QPointer or generation
     * recheck in this dispatcher. No engine/model edits or source ownership transfer;
     * comparison/dock retain appropriate source references.
     */
    void onPlotTimeSeries();

    /**
     * @brief Collect existing reports for active project and open a new owned modeless
     * delete-on-close report viewer.
     * @note Require project/model and nonempty model path, otherwise inform. Enumerate canvas 1D
     * result layers: use stored report path, or sibling output stem .rpt only when stored path
     * empty; missing nonempty stored path does not try sibling. Label scenario name when present,
     * otherwise layer name. Append settings-resolved current report for failed/no-.out runs.
     * @note Deduplicate by absolute file-path string without canonical symlink/hardlink
     * resolution; only QFileInfo exists is checked, not regular/readable/nonempty file. First
     * accepted source for active results determines initial row; an active layer sharing an
     * already accepted report does not update initial index. If none, inform with settings path.
     * Viewer copies sources, synchronously reads initial report in constructor and owns its UI;
     * no live engine/output ownership, report generation or source writes. Repeated invocation
     * opens separate viewers.
     */
    void onShowReport();

    /**
     * @brief Offer a descriptor menu for the object kind and plot the chosen attribute or
     * all-attributes sentinel.
     * @param ref Borrowed object identity; Node/Link/Subcatchment get a unit-aware picker using
     * species from the active 1D results layer. Other kinds or unavailable menu take the default
     * plot route. Popup is parent-owned and queued for deletion; cancel is inert. Target layer
     * is resolved again after menu exit, so active-project changes can affect the chosen run.
     * The reference must remain valid during synchronous nested menu execution.
     */
    void openTimeSeriesPlotFor(const SWMMObjectRef &ref);

    /**
     * @brief Offer an attribute/species menu and route the pick to an explicit results layer.
     * @param ref Borrowed object identity consumed synchronously; unsupported kinds use default
     * route.
     * @param layer Optional borrowed raw results layer. Null delegates to the active-layer
     * picker;
     * nonnull supplies species and remains the target after menu exit. Caller must ensure it
     * survives modal reentry; no local lifetime guard. Cancel is inert; menu is
     * deleteLater-owned.
     */
    void openTimeSeriesPlotForOnLayer(const SWMMObjectRef &ref, SWMMResultsLayer *layer);

    /**
     * @brief Return the temporary route target or lazily create the shared comparison view in its
     * dock.
     * @return Borrowed dock-owned view. A guarded per-call route override wins; otherwise one
     * cached view is constructed/wired and added. Requires initialized Time Series dock; does
     * not itself reveal it, bind a run or transfer ownership.
     */
    openswmmvis::ui::ComparisonPlotDialog *ensureComparisonPlotDialog();
    /**
     * @brief Create owned Time Series, 1D Profile and 2D Profile docks with stable settings
     * names.
     * Allows split docks, tabifies initial bottom layout and hides new plot docks. View selection
     * activates its nonclosing owner project. Assumes one-time construction before restoring
     * saved layout; repeating creates duplicate docks/connections rather than reusing them.
     */
    void initializePlotDocks();
    /**
     * @brief Connect comparison map-picking, close cleanup and animation cursor synchronization.
     * @param view Required borrowed dock-owned comparison view with a valid model. Connections
     * use QObject lifetimes; closing cancels its map picker and clears matching guarded caches.
     * Seeds current animation time when controller exists. Intended once per view; repeats add
     * duplicate connections. Does not adopt/delete view or select an output run.
     */
    void wireComparisonPlot(openswmmvis::ui::ComparisonPlotDialog *view);
    /**
     * @brief Restore the prior canvas tool when the plot picker is still active, then clear
     * guarded pick state.
     * Queues the owned picker for deletion and unchecks the surviving target's Add from Map
     * control. Does not replace a different tool selected meanwhile or destroy the target view.
     */
    void cancelPlotPick();

    /**
     * @brief Find or create the main window's direct-child modeless rainfall editor.
     * @return Borrowed parent-owned, delete-on-close dialog. New dialog receives the current
     * model layer even when null; existing dialog is rebound only when an active model exists,
     * so no active model does not clear an old binding here. Does not show/raise the dialog.
     */
    openswmmvis::ui::RainfallVisualizationDialog *ensureRainfallVisualizationDialog();

    /**
     * @brief Require an active project model, then show/raise/activate the shared rainfall
     * dialog.
     * Missing context logs a warning. Reused dialog is rebound by the ensure helper; no engine
     * mutation, simulation start or modal execution is performed by this entry point.
     */
    void openRainfallVisualization();

    /**
     * @brief Open the rainfall dialog and focus the named gage if a direct-child dialog can be
     * found.
     * @param ref Borrowed identity; only its name is used, without checking objectType. The
     * initial open helper may warn/return without a project, but a preexisting dialog is still
     * found and focused. No retained argument reference or new gage is created.
     */
    void openRainfallVisualizationFor(const SWMMObjectRef &ref);

    /**
     * @brief Add the default object attribute using the active project's active 1D results layer.
     * @param ref Borrowed object identity forwarded synchronously. Node defaults Depth, Link
     * Flow, Subcatchment Runoff; shared on-layer handler owns context checks and presentation.
     */
    void openComparisonPlotFor(const SWMMObjectRef &ref);

    /**
     * @brief Add one default series for a supported object and reveal the shared/routed
     * comparison view.
     * @param ref Borrowed identity; Node uses Depth, Link Flow and Subcatchment Runoff. Other
     * kinds still attach the run/reveal the plot without adding a series.
     * @param layer Optional borrowed results layer; null resolves active project's active 1D
     * layer. Requires active project/canvas even for explicit layer. Missing results prompts.
     * Raw layer must survive synchronous calls; this dispatcher does not validate project
     * membership, check returned run index or take ownership. Series validation belongs to view.
     */
    void openComparisonPlotForOnLayer(const SWMMObjectRef &ref, SWMMResultsLayer *layer);

    /**
     * @brief Convert a fixed attribute to a descriptor and dispatch against active 1D results.
     * @param ref Borrowed object identity forwarded synchronously.
     * @param attribute Fixed attribute; descriptor authority decides validity. An invalid result
     * becomes the all-attributes sentinel in the descriptor dispatcher. No argument is retained.
     */
    void openComparisonPlotForAttribute(const SWMMObjectRef &ref,
                                        openswmmvis::plot::PlotAttribute attribute);

    /**
     * @brief Convert a fixed attribute and dispatch to the explicit-or-active 1D results layer.
     * @param ref Borrowed object identity forwarded synchronously.
     * @param attribute Fixed attribute converted by ResultDescriptor::forAttribute.
     * @param layer Optional borrowed results layer; null requests the active one. Descriptor
     * handler owns context checks, fan-out and plot presentation; caller retains layer lifetime.
     */
    void openComparisonPlotForAttributeOnLayer(const SWMMObjectRef &ref,
                                               openswmmvis::plot::PlotAttribute attribute,
                                               SWMMResultsLayer *layer);

    /**
     * @brief Dispatch an object descriptor against the active project's active 1D output layer.
     * @param ref Borrowed object identity consumed synchronously.
     * @param descriptor Borrowed fixed/species descriptor; invalid means all run-supported
     * attributes/species for the object kind. No reference is retained by this wrapper.
     */
    void openComparisonPlotForDescriptor(const SWMMObjectRef &ref,
                                         const openswmmvis::plot::ResultDescriptor &descriptor);
    /**
     * @brief Attach the explicit-or-active run and add one descriptor or all run-supported
     * descriptors.
     * @param ref Borrowed Node/Link/Subcatchment identity; other kinds only reveal the plot.
     * @param descriptor Borrowed descriptor; invalid fans out using SwmmOutRunLayer's actual
     * kind descriptor list, including this run's named species.
     * @param layer Optional borrowed results layer; null resolves active project's active 1D
     * layer. Requires active project/canvas, and missing results prompts. Caller ensures raw
     * layer lifetime and intended project membership; no local ownership/guard or run-index
     * failure check. View/model owns series acceptance and stored copies.
     */
    void
    openComparisonPlotForDescriptorOnLayer(const SWMMObjectRef &ref,
                                           const openswmmvis::plot::ResultDescriptor &descriptor,
                                           SWMMResultsLayer *layer);

    /**
     * @brief Add descriptor series to the owning profile's reusable comparison view using that
     * project's active run.
     * @param profileDlg Optional borrowed profile; null/missing owner/canvas is inert. Must
     * remain
     * valid while constructing/revealing its owner-scoped view. Missing active results prompts.
     * @param ref Borrowed object identity; only Node/Link/Subcatchment adds series, others reveal
     * only.
     * @param descriptor Borrowed fixed/species descriptor; invalid expands the actual run
     * descriptor
     * list. Creates/wires dock-owned view per profile and stores guarded cache; parent profile's
     * close wiring closes it. No global-active-project source substitution or engine mutation.
     */
    void openComparisonPlotOverlayForProfile(ProfilePlotDialog *profileDlg,
                                             const SWMMObjectRef &ref,
                                             const openswmmvis::plot::ResultDescriptor &descriptor);

    /**
     * @brief Add a system-wide fixed attribute to the active 1D results comparison and reveal it.
     * @param attribute Must be a recognized system attribute; Unknown/non-system is inert.
     * No active project/canvas is inert; missing active results prompts. Does not choose the
     * first available layer or inspect returned run index; view owns series validation.
     */
    void openComparisonPlotForSystemAttribute(openswmmvis::plot::PlotAttribute attribute);

    /**
     * @brief Attach a 2D result source and add the requested cell-by-descriptor product.
     * @param layer Optional borrowed raw results layer; null is inert and lifetime must survive
     * any modal confirmation. Source attachment failure warns.
     * @param triIdxList Borrowed cell indices; empty is inert, validation/dedup belongs to view.
     * @param attrs Borrowed descriptors; empty is inert, copied/consumed by plot methods.
     * Requires active project. More than 500 requested combinations prompts; declining leaves
     * any already-created plot/run attached but adds no series. No provider geometry mutation.
     */
    void openComparisonPlotForCells(class SWMM2DResultsLayer *layer, const QVector<int> &triIdxList,
                                    const QVector<openswmmvis::plot::ResultDescriptor> &attrs);

    /**
     * @brief Arm one main-window-owned map picker for an active comparison view, or cancel
     * matching pick state.
     * @param active False cancels only for null/matching target. True requires target active,
     * active project and canvas; failure unchecks target. Same target/tool is reused.
     * @param target Optional borrowed guarded comparison destination; null cannot activate.
     * Saves previous tool, seeds species from active results and routes picked descriptors/
     * objects/system variables through a scoped destination override. Escape/close cancels;
     * context may change before a pick, so result dispatch performs its normal active checks.
     */
    void onAddFromMapToggled(bool active, openswmmvis::ui::ComparisonPlotDialog *target = nullptr);

    /**
     * @brief Create a separate docked 1D profile view owned by its plot dock and associated
     * project.
     * @param path Borrowed network path copied/consumed by view; at least two node indices and
     * a live matching model engine are required. Missing project is inert; missing model prompts.
     * Node indices are used for engine title lookup without validation here. Wires attribute
     * requests to a per-profile comparison view, closed when the profile closes.
     */
    void openProfilePlotFor(const ProfileRouter::Path &path);

    /**
     * @brief Open an analysis mesh profile with the active project's active 2D results when
     * present.
     * @param scenePolyline Borrowed scene-coordinate trace forwarded to the shared builder.
     * Missing results allows its ordinary authored-mesh ground-only fallback; no forced run load.
     */
    void openMeshProfilePlotFor(const QVector<QPointF> &scenePolyline);
    /**
     * @brief Open a ground-only authored-mesh profile through the shared builder, without primary
     * results.
     * @param scenePolyline Borrowed scene trace consumed synchronously; builder checks
     * size/context.
     * Does not use the active results layer as primary or change mesh geometry.
     */
    void openMeshBedProfilePlotFor(const QVector<QPointF> &scenePolyline);

    /**
     * @brief Show a project-owned modeless DeleteOnClose terrain profile for a traced path.
     * @param[in] scenePolyline Borrowed polyline in canvas scene coordinates, copied into the
     * plot; fewer than two points is ignored.
     * Requires an active project and the terrain toolbar selected raster, otherwise
     * returns/prompts. Passes current raster-to-model vertical factor, binds subsequent factor
     * updates only while that same raster stays selected, and leaves raster/project externally
     * owned. Does not retrace against a newly selected raster or mutate elevations.
     */
    void openTerrainProfilePlotFor(const QVector<QPointF> &scenePolyline);

    /**
     * @brief Arm the active project's network profile selection tool.
     * No project is inert. Missing network informs and unchecks the profile action when present;
     * otherwise delegates activation. Does not open a surface profile or build a path itself.
     */
    void onPlotProfileTriggered();

    /**
     * @brief Create or reveal an owner-scoped 2D section profile using matching authored
     * mesh/results.
     * @param scenePolyline Borrowed scene-coordinate trace with at least two points; shorter is
     * inert.
     * @param results Optional borrowed primary result layer; null permits a bed-only authored
     * mesh.
     * @param title Borrowed optional title copied into a new view; empty keeps constructor title.
     * @param owner Optional borrowed project; null resolves active. No owner means no action.
     * @param definition Optional borrowed saved section consumed synchronously. Matching owner/ID
     * reveals existing view; mesh primaries match stored path or layer ID. Result primaries never
     * fall back to authored mesh. Missing geometry informs; definition refusal deletes the new
     * view and warns. View/store connections handle saved definitions. No raw inputs are owned
     * by this dispatcher; caller must preserve context across warnings and nested dialogs.
     */
    void openMeshProfileDialog(const QVector<QPointF> &scenePolyline,
                               class SWMM2DResultsLayer *results, const QString &title,
                               SWMMVisProjectWindow *owner = nullptr,
                               const ProfileSection::Definition *definition = nullptr);
    /**
     * @brief Resolve a saved section by ID and open it only with its matching primary source.
     * @param owner Optional borrowed project; null is inert.
     * @param id Borrowed saved section ID; absent ID is inert. Compares absolute file paths to
     * loaded result sources, without canonical symlink resolution. Result sections with missing
     * primary source inform instead of substituting another run; mesh sections delegate matching
     * to openMeshProfileDialog. No definition mutation or ownership transfer.
     */
    void openSavedMeshProfile(SWMMVisProjectWindow *owner, const QString &id);

    /**
     * @brief Plot one requested edge attribute against the active project's active 2D results
     * layer.
     * @param mesh Borrowed nominal mesh, deliberately unused; no identity/geometry match check.
     * @param triIdx Triangle index in the results source's mesh, forwarded without local
     * validation.
     * @param edgeLocal Local edge ordinal, forwarded without local range checking.
     * @param attr Requested plot attribute, forwarded without enforcing an edge-only enum here.
     * Requires project, source and edge-flux availability; missing data informs, attachment
     * failure warns. Reveals the shared comparison on success; view validates actual series.
     */
    void openMeshEdgeFluxPlotFor(class SWMM2DMeshLayer *mesh, int triIdx, int edgeLocal,
                                 openswmmvis::plot::PlotAttribute attr);

    /**
     * @brief Plot every requested vertex/attribute combination from the active 2D results source.
     * @param mesh Borrowed nominal mesh, deliberately unused; source geometry must match indices.
     * @param vertexIdxList Borrowed indices; empty is inert and indices are not locally
     * validated.
     * @param attrs Borrowed attributes; empty is inert, each converted to a descriptor.
     * Requires project/source and successful run attachment. Over 500 combinations prompts;
     * caller must keep referenced input/layer state valid through nested execution. View owns
     * series validation/dedup; no mesh edits or input-file writes.
     */
    void openMeshVertexSeriesFor(class SWMM2DMeshLayer *mesh, const QVector<int> &vertexIdxList,
                                 const QVector<openswmmvis::plot::PlotAttribute> &attrs);

    /**
     * @brief Rebind shared panels, analysis transport, selection/property routes and status
     * controls when a different project tab becomes active.
     * @param[in] window Borrowed MDI subwindow, possibly null or non-project. Context remains
     * owned by MDI/project; no ownership transfer. GUI-thread callback requires fully initialized
     * required UI controls.
     * @note Refresh tab tooltips/title first. Null OR non-project input preserves current
     * bindings when any nonclosing project is still listed (a transient dock-focus/Welcome
     * activation); a non-project title can already become bare SWMMVis before that return. When
     * no live project remains, cancel plot pick, clear listed panel/tool contexts, active global
     * units and active-project cache, disable project/edit controls and clear undo binding. This
     * clear branch does not itself explicitly reset animation-controller sources, section-view
     * project, active-results combos or all old canvas signal connections.
     * @note Same cached project returns after tooltip/title work without refreshing remaining
     * bindings. A real switch cancels cross-canvas pending plot pick, restores per-project
     * dock/undo/edit/tool/terrain state, sets global UnitSystem facade, and blocks action/control
     * signals while mirroring values so the UI refresh does not write flow-unit/offset options.
     * @note If incoming cached active result pointer is null, choose first matching canvas 1D/2D
     * layer (without readiness filtering), then bind animation primary/fallback, tree checks,
     * result combos and property/table statistics. Thus a deliberately cleared null selection can
     * be repopulated on switch. Later source changes refresh active-tab views; no engine data is
     * sampled for plots here.
     * @note Reconnect active-project lambdas after disconnecting ALL connections for selected
     * sender/signal/receiver triples (canvas layerAdded/layerRemoved/activeToolChanged, project
     * result/edit signals, model options and selection manager). Other same-receiver subscribers
     * on those triples must use a separate QObject context to survive. Ordinary PMF links use
     * UniqueConnection; 2D cell-plot request is queued so mouse gesture completes before any
     * modal prompt. Old background-project guarded lambdas remain but ignore inactive state; some
     * ordinary status/canvas PMF connections remain across tabs.
     * @note Selection callback uses arbitrary first QSet member for section/data/spatial property
     * view. Single mesh selection shows mesh adapter; multiple mesh clears. Feature selection
     * returns without replacing existing Properties content (attributes belong to
     * Features/Attribute Table). Initial binding does not explicitly invoke this selection
     * callback. Mesh-toolbar name refresh is queued for data-object mutation; terrain controls
     * propagate selected raw raster and model-unit offsets to project setters.
     * @note No source/layer ownership transfer, explicit simulation-state gate or save operation.
     * Binding can synchronously emit refresh signals and default-result selection changes;
     * project/canvas/model must remain live throughout GUI callback.
     */
    void onActiveSubWindowChanged(QMdiSubWindow *window);

    /**
     * @brief Refresh MDI tab tooltips from each project window's full model path through shared
     * chrome helper.
     * Nonproject tabs supply empty text. Requires initialized UI/MDI area; no title,
     * active-project
     * or filesystem changes, and no project ownership transfer.
     */
    void refreshProjectTabToolTips();

    /**
     * @brief Set main title from a project's current title and absolute containing folder.
     * @param pw Optional borrowed project; null uses bare SWMMVis. Pathless uses project title
     * only; file-backed uses native folder separators. No model/file mutation or pointer
     * retention.
     */
    void syncMainWindowTitle(SWMMVisProjectWindow *pw);

    /**
     * @brief Replace Window menu dynamic tail from current MDI project list and registered
     * modeless dialogs; absent menu is a no-op.
     * @note Keep first three Minimize/Zoom/separator actions; remove and deleteLater every later
     * action, clearing cached tail pointers before recreation so rebuilding inside an action
     * emission is safe. Lists only project subwindows (including any still-listed closing
     * project), checkmarks active tab, then registered dialogs newest-first with
     * title/objectName/Untitled fallback. Menu actions are owned by menu.
     * @note Project entry activates/raises/focuses captured raw project; expected MDI/menu
     * rebuild lifecycle must keep it live until triggered. Dialog entry retains QPointer, clamps
     * to connected screen before show/raise/activate and safely ignores destroyed dialog. Bring
     * All raises projects, main window then registry stack; Reset delegates saved-geometry
     * recovery. No project saves or document mutation.
     */
    void rebuildWindowMenu();

    /**
     * @brief Remove persisted dialog/main geometry, restore main window from maximized/fullscreen
     * if needed, and gather registered dialogs onto its current screen.
     * @note Deletes only Dialogs/&lt;direct child group&gt;/geometry and
     * SWMMVis::MainWindow/SWMMVis::Geometry, then syncs settings without checking IO status.
     * Keeps dock/toolbar/splitter/header/page state and all project data. Layout watchers may
     * persist new visible geometry later.
     * @note Clamp main window onto a connected screen, take guarded registry snapshot and
     * clamp/raise each still-live dialog using main screen; does not traverse arbitrary
     * unregistered top-level windows or move MDI project content separately. No
     * confirmation/undo; logs completion text even if settings sync had an error. GUI thread
     * only.
     */
    void resetWindowPositions();

protected:
    /**
     * @brief Run dirty/untitled project close prompts, then close dialogs, cancel jobs and save
     * UI settings.
     * @param event Required borrowed Qt close event, ignored on the first project close refusal.
     * Earlier projects may already have closed if a later one cancels; no atomic rollback.
     * After prompts, drains this object's posted metacalls to surface queued save warnings,
     * closes top-level QDialogs, closes checkpoint contexts and unpauses/cancels runners before
     * base handling. Does not wait synchronously for jobs or inspect dialog-close refusals.
     * Requires live UI/models and GUI thread; raw widget snapshots rely on close ownership rules.
     */
    void closeEvent(QCloseEvent *event) override;
    /**
     * @brief Track content focus for Copy dispatch and implement nondeleting Welcome-tab closure.
     * @param watched Borrowed QObject being filtered; QWidget ancestry determines ownership and
     * menu/toolbar chrome. Focus/mouse events can update guarded content-focus cache.
     * @param event Required borrowed Qt event. Welcome Close hides subwindow/tab, maximizes and
     * activates the first other visible subwindow, and is consumed without destroying content.
     * @return True for the handled Welcome close, otherwise base eventFilter result. Other
     * focus events only update cache; automatic menu/popup focus does not replace Copy target.
     */
    bool eventFilter(QObject *watched, QEvent *event) override;

    /**
     * @brief Create the main-window dock-toggle popup from currently available named actions.
     * @return Newly allocated main-window-owned menu sharing existing dock actions; caller/Qt may
     * display it. Omits tab-managed toolbar toggles and missing names, without taking QAction
     * ownership.
     */
    QMenu *createPopupMenu() override;

    // Feature A — drag-and-drop of .inp / .oswp files onto the main window.
    // Accepted local paths are routed through onOpenProject().
    /**
     * @brief Accept the proposed drag action only when the payload contains at least one
     * supported local file.
     * @param event Required borrowed Qt drag-enter event; inspected/updated synchronously.
     * Unsupported payload is ignored. Does not open files or delegate to base handling.
     */
    void dragEnterEvent(QDragEnterEvent *event) override;
    /**
     * @brief Reevaluate supported local paths and accept or ignore the proposed moving drag.
     * @param event Required borrowed Qt drag-move event; not retained. No file loading occurs.
     */
    void dragMoveEvent(QDragMoveEvent *event) override;
    /**
     * @brief Accept supported local-file drops and dispatch each path through onOpenProject.
     * @param event Required borrowed Qt drop event; no supported files causes ignore/return.
     * Dispatch preserves payload order; project opening handles duplicate windows/extensions
     * and may be asynchronous. Does not retain MIME pointers or aggregate a drop result.
     */
    void dropEvent(QDropEvent *event) override;

    /**
     * @brief Collect existing local .inp/.oswp files from a URL drag payload in payload order.
     * @param mime Optional borrowed MIME object consumed synchronously; null/no URLs yields
     * empty.
     * @return Owned path list, preserving duplicates. Extension match ignores case; remote URLs,
     * directories and nonexistent paths are skipped. Does not parse content or open any path.
     */
    [[nodiscard]] QStringList acceptableDropPaths(const QMimeData *mime) const;

    /**
     * @brief Find the MDI subwindow whose inner widget is the generated welcome widget.
     * @return Borrowed MDI-owned subwindow or null without UI/widget/area/membership. Does not
     * show it, validate visibility, create a replacement or extend its lifetime.
     */
    QMdiSubWindow *welcomeSubWindow() const;

    /**
     * @brief Request close on the existing welcome subwindow when present.
     * The installed main-window event filter normally consumes this close and hides tab/content
     * in place; this helper does not itself remove it from MDI, delete it or null the UI pointer.
     */
    void removeWelcomeSubWindow();

private:
    Ui::SWMMVis *ui;
    QStringList mRecentFiles;
    bool mShowSplashScreenOnStartUp = true;
    bool mShowWelcomeScreenOnStartUp = true;

    // Status bar widgets
    QCheckBox *mCheckBoxLevelOffsetMode = nullptr;
    QLabel *mLabelOffsetElevation = nullptr;
    QLabel *mLabelOffsetDepth = nullptr;
    QCheckBox *mCheckBoxAutoLength = nullptr;
    QToolButton *mToolButtonCoordinateReferenceSystem = nullptr;
    QLineEdit *mLineEditCoordinates = nullptr;
    // Coordinate-readout state: last cursor position (canvas CRS) + the
    // mesh hover-Z mirrored from the Mesh Editing toolbar's probe, so
    // either input can rebuild the readout.
    double mLastCursorMapX = 0.0;
    double mLastCursorMapY = 0.0;
    bool mHasCursorPos = false;
    std::optional<double> mMeshHoverZ;
    QComboBox *mComboBoxMapScale = nullptr;
    QComboBox *mComboBoxFlowUnits = nullptr;
    QComboBox *mComboBoxEngineVersion = nullptr;
    QProgressBar *mProgressBar = nullptr;
    /*! Stage text shown beside \ref mProgressBar ("Parsing 2D mesh…").
     *  Hidden whenever the bar is. */
    QLabel *mProgressLabel = nullptr;
    QDateTimeEdit *mDateTimeEditAnimationTime = nullptr;
    QLabel *mLabelAnimationSpeed = nullptr;
    QComboBox *mComboAnimationSpeed = nullptr;
    QCheckBox *mCheckBoxAnimationCycle = nullptr; // loop playback at end-of-range (default on)
    // Causal "as-of within timespan" sync controls (look-back window).
    QLabel *mLabelAnimationWindow = nullptr;
    QDoubleSpinBox *mSpinAnimationWindow = nullptr;
    // Issue 1 — single-thumb scrubber (cursor) + painted look-back window band.
    // Replaces the laggy two-thumb RangeSliderWidget; the window is driven by
    // mSpinAnimationWindow, not by a second thumb.
    openswmmvis::ui::CursorWindowSlider *mAnimationSlider = nullptr;
    // 2026-07-19 — scrub-seek coalescing (slider lag fix). cursorChanged
    // fires per drag pixel; each seek used to run the full synchronous
    // layer fetch + 2D frame load. Drag now stores the pending time and
    // restarts this ~40 ms single-shot timer (trailing edge, same idiom as
    // MapCanvas::m_refreshTimer); cursorReleased seeks immediately so the
    // final frame matches the thumb exactly.
    QTimer *mScrubCoalesceTimer = nullptr;
    QDateTime mPendingSeekTime;
    // Active analysis-layer selectors on the Analysis toolbar. The user picks
    // which 1D / 2D results layer every analysis tool targets; "— none —"
    // returns to model editing. Populated from the active project window's
    // OutputStatsRegistry (1D) / canvas 2D results layers, kept in sync via
    // SWMMVisProjectWindow::activeResultsLayerChanged / active2DResultsLayerChanged.
    QLabel *mLabelActiveResults1D = nullptr;
    QComboBox *mComboActiveResults1D = nullptr;
    QLabel *mLabelActiveResults2D = nullptr;
    QComboBox *mComboActiveResults2D = nullptr;
    QCheckBox *mCheckBoxLiveRendering = nullptr;
    class AnimationController *mAnimationController = nullptr;
    class TerrainToolbar *mTerrainToolbar = nullptr;
    class MeshEditingToolbar *mMeshEditingToolbar = nullptr; // Slice §V.VB

    // UI redesign P6 — tabbed compact toolbar (strip + per-tab rows).
    openswmmvis::ui::CompactToolbarController *mCompactToolbar = nullptr;
    /** @brief Initialize native GUI extensions after host controls exist and before startup model restore.
     * @note GUI thread only. Builds scoped SDK hosts and UI bridges; safe mode skips plugin code.
     * @see gui_extension_sdk */
    void initializeExtensions();
    class QToolBar *mToolBarHome = nullptr;
    class QToolBar *mToolBarModel = nullptr;
    class QToolBar *mToolBarMesh2D = nullptr;
    class QToolBar *mToolBarView = nullptr;
    class QToolBar *mToolBarFeatures = nullptr;
    // Iteration 2 (R3) — the two formerly .ui-authored bars, rebuilt in
    // code with the SAME objectNames so saved window state keeps working.
    class QToolBar *mToolBarAnimation = nullptr;
    class QToolBar *mToolBarAnalysis = nullptr;
    // Ribbon groups later phases re-anchor into: the analysis layer
    // combos, the animation timeline widgets, and the Plots group whose
    // Plot-Profile button carries the override dropdown.
    openswmmvis::ui::RibbonGroup *mGroupResultsLayers = nullptr;
    openswmmvis::ui::RibbonGroup *mGroupTimeline = nullptr;
    openswmmvis::ui::RibbonGroup *mGroupPlots = nullptr;
    QMetaObject::Connection mMeshTabConnAdd;
    QMetaObject::Connection mMeshTabConnRemove;

    QSettings mSettings;
    QStandardItemModel *mLogMessagesModel = nullptr;
    /*! Perf plan B2 — one Message-Log scrollToBottom per event-loop turn,
     *  not one per appended row (engine-warning drains are bursts). */
    bool mLogScrollPending = false;
    LayerTreePanel *mLayerTreePanel = nullptr;
    ObjectBrowserPanel *mObjectBrowserPanel = nullptr;
    PropertiesPanel *mPropertiesPanel = nullptr;
    // Slice SP.4 — dockable vector section / profile view of the selection.
    openswmmvis::ui::SectionViewPanel *mSectionViewPanel = nullptr;
    AttributeTablePanel *mAttributeTablePanel = nullptr;
    // Editable feature layers — the Features dock and its host dock widget
    // (MESH_DIALOG_TABS_AND_FEATURE_LAYERS_PLAN §5.1). Tabified behind the
    // property browser; the panel picks which FeatureLayer the drawing tools
    // target.
    openswmmvis::ui::FeatureLayerPanel *mFeatureLayerPanel = nullptr;
    class QDockWidget *mFeatureDock = nullptr;
    openswmmvis::ui::PlotDock *mTimeSeriesDock = nullptr;
    openswmmvis::ui::PlotDock *mProfiles1DDock = nullptr;
    openswmmvis::ui::PlotDock *mProfiles2DDock = nullptr;
    QPointer<openswmmvis::ui::ComparisonPlotDialog> mComparisonPlot;
    QHash<ProfilePlotDialog *, QPointer<openswmmvis::ui::ComparisonPlotDialog>> mProfileComparisons;
    QPointer<openswmmvis::ui::ComparisonPlotDialog> mComparisonRouteTarget;
    QPointer<openswmmvis::ui::ComparisonPlotDialog> mPlotPickTarget;
    QPointer<MapCanvas> mPlotPickCanvas;
    SimulationStatusModel *mSimStatusModel = nullptr;
    // Slice BB Phase 8.6.11 / 8.6.16 — dockable per-class legend / style editor.
    openswmmvis::ui::LegendDock *mLegendDock = nullptr;
    // Slice Z.18 — always-open layer-styling editor dock.
    openswmmvis::ui::LayerStylingDock *mLayerStylingDock = nullptr;

    // Live simulation-progress tracker for the status-bar bottom bar.
    // Keyed by SimulationRunner jobId, value is the latest 0.0–1.0
    // fraction reported by the engine progress callback. The bar shows
    // min-across-running-jobs as a real percent (never busy).
    QHash<int, double> mRunningSimProgress;

    // Keyed map of active simulation runners by jobId so the Pause /
    // Cancel toolbar actions can reach any running runner. Entries are
    // removed on SimulationRunner::finished.
    QHash<int, class SimulationRunner *> mActiveRunners;
    QHash<int, QUuid> mLatestRunAttempts; ///< Current attempt epoch for each reusable status row.
    QHash<QUuid, QPointer<SWMMVisProjectWindow>> mRunAttemptOwners; ///< Counted run/preparation reservations.
    QHash<QString, QUuid> mPreparingRunAttempts; ///< Session/engine preparations, before runner creation.
    QHash<QUuid, QUuid> mLastAcceptedRunAttempts; ///< Last accepted UUID per session, including preparation failure.
    bool mRunControllerShuttingDown = false; ///< Reject new attempts while accepted native work drains.
    /** @brief Run native preflight for one captured model generation.
     * @param[in] sessionId Expected SDK model session, or null for the active native model.
     * @param[in] loadGeneration Exact requested generation, or zero for the active native model.
     * @details Captures the generation before save/overwrite dialogs and refuses a changed generation
     * before accepting preparation. */
    void runSimulationForGeneration(const QUuid &sessionId, quint64 loadGeneration);
    /** @brief Test the persistent row epoch, independently of the active runner map.
     * @param[in] rowId Reusable presentation row.
     * @param[in] operationId Captured unique attempt.
     * @return True only while this attempt still owns that row's native state. */
    bool runAttemptCurrent(int rowId, const QUuid &operationId) const;
    /** @brief Release exactly one counted preparation/runner reservation.
     * @param[in] operationId Captured unique attempt; repeated calls are inert. */
    void releaseRunAttempt(const QUuid &operationId);
    /** @brief Request cancellation without retargeting a reused status row.
     * @param[in] operationId Unique accepted native attempt.
     * @param[out] error Optional refusal diagnostic.
     * @return True for accepted/idempotent active cancellation. */
    bool requestRunCancellation(const QUuid &operationId, QString *error = nullptr);
    /** @brief Start one exact Ready native model generation through normal preflight.
     * @param[in] sessionId Per-open model identity.
     * @param[in] loadGeneration Exact current accepted generation.
     * @param[out] error Optional refusal diagnostic.
     * @return Accepted attempt UUID, including a synchronous preparation failure, or null. */
    QUuid startRunAttemptForSession(const QUuid &sessionId, quint64 loadGeneration,
                                   QString *error = nullptr);
    QHash<SWMMVisProjectWindow *, CheckpointModel *> mCheckpointModels;
    QHash<SWMMVisProjectWindow *, QPointer<CheckpointsDialog>> mCheckpointDialogs;

    // Slice CF.MVP — keyed map of in-flight 2D results layers so per-tick
    // twoDDepthsAvailable signals can find the layer to push into. Uses
    // QPointer so the entry is auto-cleared if the layer is destroyed (e.g.
    // the user closes the project window mid-run).
    QHash<int, QPointer<class SWMM2DResultsLayer>> mActive2DResultsLayers;

    /**
     * @brief Finish tracked live 2D layer and replace its source with saved HDF5 history when
     * usable; otherwise retain in-memory history.
     * @param[in] jobId Finished run correlation ID; absent active-layer entry is a no-op,
     * including leaving any start-time cache untouched.
     * @param[in] success True for normal runner completion; false with errCode zero means
     * cooperative/legacy-classified cancellation.
     * @param[in] errCode Nonzero with false success is genuine failure: skip rename/source swap
     * and only remove tracking/start cache. If success is true, code value does not prevent
     * adoption.
     * @param[in] operationId Captured attempt UUID; a stale attempt cannot adopt results for a
     * reused status row. Null uses the currently tracked attempt for direct native calls.
     * @note Caller must serialize on GUI thread and ensure no exporter currently reads the old
     * source. This helper itself does not test mExport2DInFlight or mark source non-live;
     * finished callback does that before calling/defer. Uses per-job guarded layer, not active
     * project, and does not require origin project still active.
     * @note Success/cancel renames layer final/partial before opening stored snoopy_h5_path;
     * missing file or silent open failure retains old bounded in-memory source. On open, set
     * saved simulation start when known, scan every readable depth frame for strictly greatest
     * positive cell depth (metres), then transfer unique source ownership to layer. Failed/empty
     * frames are ignored; ties retain the earliest peak. No scientific finite-data validation
     * beyond source read/max behavior.
     * @note Positive peak sets max color depth without changing dry-depth or thin-film policies.
     * Preserve the selected shared time, or the held time on unselected layers, across the
     * source swap. Live following without a 1D driver displays the final frame.
     * Does not mark project dirty, save sidecar or refresh layer CRS. Removes active-job
     * layer/start maps for every tracked outcome after work; external layer itself remains on
     * canvas.
     */
    void adoptFinished2DResults(int jobId, bool success, int errCode,
                               const QUuid &operationId = {});

    /*! True while onExport2DResults holds a live source (dialog + write).
     *  Finished jobs arriving meanwhile queue in mDeferred2DFinish
     *  (jobId → {operationId, success, errCode}) and are adopted when the export returns. */
    bool mExport2DInFlight = false;
    struct Deferred2DFinish {
        QUuid operationId; ///< Captured attempt epoch, never inferred from a later reused row.
        bool success = false; ///< Native terminal result.
        int errorCode = 0; ///< Native terminal diagnostic code.
    };
    QHash<int, Deferred2DFinish> mDeferred2DFinish;

    /*! Live 1D results (LIVE_1D_RESULTS_PLAN_V2 §4.3): per running job, the
     *  SWMMResultsLayer tailing its .out. Filled on the first progress tick
     *  whose header is on disk, refreshed on every later tick, released in
     *  the finished handler (which finalises the same layer in place). */
    QHash<int, QPointer<class SWMMResultsLayer>> mLive1DLayers;

    /**
     * @brief Reuse an output layer by absolute path or create/add one on a model project canvas.
     * @param pw Optional borrowed project; requires canvas and model layer, otherwise returns
     * null.
     * @param outPath Borrowed results path compared via absoluteFilePath, without existence,
     * canonical symlink or case normalization checks. New layer stores it but is not opened here.
     * @param rptPath Borrowed report path copied into reused/new layer, including empty values.
     * @return Borrowed project/model-owned layer, added visible when new. No active-layer
     * promotion, output parsing or ownership transfer; repeated equal paths reuse first match.
     */
    class SWMMResultsLayer *findOrCreateResultsLayer(SWMMVisProjectWindow *pw,
                                                     const QString &outPath,
                                                     const QString &rptPath);

    /**
     * @brief Open or refresh a running output layer and promote it when data first appears.
     * @param jobId Running job key in guarded live-layer cache; caller supplies the matching job.
     * @param pw Borrowed live project; required for first creation and any data promotion.
     * @param outPath Borrowed path used only until a live layer is cached; then cache wins.
     * @param rptPath Borrowed report path forwarded during find/create only.
     * @param operationId Captured attempt UUID, or null to use the current native row attempt;
     * stale callbacks leave a later attempt's layer and selection untouched.
     * Header-open failures silently retry on later calls and may leave an unopened canvas layer.
     * First data stretches ramp, sets active results and animation primary; requires initialized
     * animation controller. No completion finalization, file write or runner cancellation.
     */
    void tickLive1DResults(int jobId, SWMMVisProjectWindow *pw, const QString &outPath,
                          const QString &rptPath, const QUuid &operationId = {});

    // Per-job simulation start time captured from simulationDatesKnown so the
    // post-run HDF5Mesh2DSource can map its /time (days-since-start) to
    // calendar QDateTime for the global animation slider.
    QHash<int, QDateTime> mSimulationStarts;

    // Slice AT.2 — Add-from-Map mode state. When active, mPlotPickTool is
    // the canvas's active tool and mPrevMapTool holds whichever tool was
    // active when the user toggled "Add from Map…" on. Both cleared when
    // the user toggles back off / presses Esc.
    QPointer<class OpenSWMMVisMapToolPlotPick> mPlotPickTool;
    QPointer<class OpenSWMMVisMapTool> mPrevMapTool;

    // ── Status-bar progress arbitration (LOAD_PERF Phase 1c) ──────────────
    // One QProgressBar serves three producers: simulation runs (real
    // percent), project opens (real percent), and a handful of genuinely
    // unmeasurable operations (raster/table open) that still want a spinner.
    // They are mutually exclusive, so ownership is explicit and every writer
    // goes through applyProgressBarState().

    /*! Who currently owns \ref mProgressBar. Simulation outranks Open: a run
     *  is the longer, explicitly user-initiated task, and a 200 ms open must
     *  not blank a 40-minute run's percentage. */
    enum class ProgressOwner { None, Busy, Open, Simulation };

    /*! In-flight project opens keyed by open id. The bar shows the MINIMUM
     *  percent across them, matching the min-across-jobs rule the simulation
     *  path already uses, so a multi-model .oswp reports the laggard. */
    QHash<int, class OpenProgressModel *> mRunningOpens;
    int mNextOpenId = 1;
    int mBusyRefCount = 0; //!< nesting depth of onSetProgressBarBusy(true)

    /**
     * @brief Arbitrate the shared progress widget with Simulation above Open above Busy above
     * hidden.
     * Simulation shows rounded/clamped minimum job fraction; open shows minimum percent and its
     * stage label; busy is indeterminate. During simulation a nonempty concurrent open label
     * can still appear, with hash iteration deciding ties/label choice. Requires finite sensible
     * simulation fractions and live registered models; missing bar is inert. Does not advance,
     * finish or cancel work or change ownership of tracked jobs.
     */
    void applyProgressBarState();

    /**
     * @brief Allocate/register a main-window-owned open-progress model with a monotonically
     * assigned ID.
     * @return Borrowed model; finish retires it through endOpenProgress. Progress changes update
     * shared bar and creation updates immediately. Caller must not delete behind registered raw
     * entry or retain the pointer after finished/deleteLater. Does not start parsing or busy
     * count.
     */
    OpenProgressModel *beginOpenProgress();

    /**
     * @brief Remove one open-progress entry and queue its owned model for deletion, then
     * recompute bar.
     * @param openId Registered ID; unknown IDs simply recompute. Does not cancel a worker or
     * finish other models. External references become invalid after queued deletion.
     */
    void endOpenProgress(int openId);

    /**
     * @brief Recompute shared progress through its arbitration funnel without changing tracked
     * job fractions.
     * Does not force simulation ownership over an empty job map or mutate worker state.
     */
    void updateSimulationProgressBar();

    /**
     * @brief Log set solver-thread environment variables with queried hardware/OpenMP limits.
     * Checks OMP_NUM_THREADS, OMP_THREAD_LIMIT, OPENSWMM_2D_THREADS and SWMM_DW_THREADS; emits
     * warning messages once per call without modifying variables or thread counts. Intended after
     * the message log exists; ignores thread-info query status.
     */
    void logInheritedThreadEnvironment();

    /**
     * @brief Ensure an idle model/version status row and populate valid engine START/END date
     * options.
     * @param pw Optional borrowed live project; requires model/engine and status model. Existing
     * active runner for ensured job owns its dates, so return without overwriting them. Invalid
     * date parsing leaves previous dates unchanged. No simulation start or project mutation.
     */
    void refreshSimulationDatesForProject(class SWMMVisProjectWindow *pw);

    /**
     * @brief Refresh Properties offset naming and Attribute Table headers after LINK_OFFSETS
     * changes.
     * Skips absent panels; does not change offset values, engine convention or project dirty
     * state.
     */
    void refreshOffsetModeViews();

    /**
     * @brief Remove project status rows and associated progress/time/2D live-map entries,
     * requesting runner cancellation.
     * @param pw Optional borrowed project; absent context/status model or no rows is inert.
     * Cancellation is asynchronous: runner entries and output reservations remain until finish.
     * Does not remove live-1D cache here, unpause a paused worker, delete files or wait for exit.
     * Recomputes bar; Pause/Cancel reset only when active-runner map is already empty.
     */
    void clearSimulationStatusForProject(class SWMMVisProjectWindow *pw);
    /**
     * @brief Show/reuse a modeless checkpoint catalog for the active saved native 6.0.0 project.
     * Missing context is inert; legacy engine/path/folder errors inform/warn. First use creates
     * model/catalog folder, chooses output profile from 2D output configuration and wires run
     * status/progress/notices plus guarded project invalidation. Project destruction closes its
     * model, deferring deletion while busy. Dialog preparation refreshes inventory or saves/
     * hashes frozen model and refuses concurrent ordinary run. Cached capability query feeds
     * model; no checkpoint run starts merely by showing the dialog. May create catalog directory.
     */
    void showCheckpoints();
    /**
     * @brief Save/hash a current-engine model, validate ownership preview and populate its
     * checkpoint request context.
     * @param project Borrowed optional project, guarded locally with QPointer;
     * requires nonclosing model with engine version exactly 6.0.0.
     * @param model Required borrowed
     * CheckpointModel, not guarded against deletion during modal warnings.
     * @return True after setting context with SHA-256, edit revision, optional ownership token
     * and cross-project path inventory; false on unavailable context, save/read/hash/ownership
     * failure. Can save a dirty model before a later failure; no rollback of that save. Feedback
     * is modal where provided. Does not launch a checkpoint operation or reserve output paths.
     */
    bool prepareCheckpointContext(SWMMVisProjectWindow *project, CheckpointModel *model);
    /**
     * @brief Append protected input/catalog paths and active output/reservation paths across all
     * project contexts.
     * @param inputs Borrowed mutable list; existing entries are preserved then deduplicated with
     * model/sidecar/mesh paths, runner inputs and checkpoint-file paths appended.
     * @param active Borrowed mutable list; appends visible result sources, runner outputs,
     * checkpoint reservations and RunPathLease active paths, then deduplicates.
     * Requires initialized MDI area/live registered contexts. Exact string equality drives
     * removal of duplicates; no canonicalization, filesystem scan, reservation or file deletion.
     */
    void checkpointInventory(QStringList &inputs, QStringList &active) const;

    // Phase 2 editing actions — declared for future programmatic wiring
    QAction *mActionAddJunction = nullptr;
    QAction *mActionAddOutfall = nullptr;
    QAction *mActionAddStorage = nullptr;
    QAction *mActionAddConduit = nullptr;
    QAction *mActionDeleteObject = nullptr;

    // macOS-style Window menu (programmatically built — the .ui has no
    // menuWindow). Entries rebuild on every MDI subWindowActivated and on
    // each project window's windowTitleChanged so the list stays in sync.
    QMenu *mMenuWindow = nullptr;
    QAction *mActionWindowMinimize = nullptr;
    QAction *mActionWindowZoom = nullptr;
    QAction *mActionWindowBringAllToFront = nullptr;
    QAction *mActionWindowResetPositions = nullptr;

    OpenSWMMVisWorkspace *mProject = nullptr;
    SWMMVisProjectWindow *mActiveProjectWindow =
        nullptr;                         // last-bound project; survives transient focus loss
    QPointer<QWidget> m_copyFocusWidget; // content focus retained while menus are active

    /**
     * @brief Resolve the active project, preferring the cached last-bound window over transient
     * MDI focus.
     * @return Borrowed cached project or the current subwindow cast to SWMMVisProjectWindow;
     * null when neither exists. Cached pointer lifetime depends on project-close wiring;
     * this lookup does not itself validate an arbitrary stale raw pointer or take ownership.
     */
    SWMMVisProjectWindow *activeProjectWindow() const;
    /**
     * @brief Resolve the current project canvas through activeProjectWindow.
     * @return Borrowed project-owned canvas, or null without an active project. No lifetime
     * extension; callers must recheck context after nested dialogs or asynchronous work.
     */
    MapCanvas *activeCanvas() const;

    /**
     * @brief Declare selection-fit support for visible SWMM model layers.
     * @param c Borrowed target canvas intended to receive the union extent; no ownership
     * transfer.
     * @return Intended true when selected objects produce an extent, false so caller may fall
     * back to full extent. This declaration currently has no definition in maintained sources
     * and no call sites; linking a new caller requires implementing the declared operation.
     */
    bool zoomCanvasToSelection(MapCanvas *c) const;
};

#endif // SWMMVIS_H
