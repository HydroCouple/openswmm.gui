/**
 * @file      propertiespanel.h
 * @brief     A dock widget that displays and edits object attributes using QPropertyModel.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @details
 * @date      2026
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef PROPERTIESPANEL_H
#define PROPERTIESPANEL_H

#include "layers/swmmmodellayer.h"

#include <QDockWidget>
#include <QList>
#include <QPointer>
#include <QVariantMap>

class QTreeView;
class QComboBox;
class QPushButton;
class QAbstractItemModel;
class QAbstractItemDelegate;
class MapCanvas;
struct SWMMObjectRef;
#ifdef HAVE_QPROPERTYMODEL
class QPropertyModel; // from QPropertyModel library
class QPropertyItemDelegate;
#endif
class OpenSWMMVisLayer;
class SWMMModelLayer;
class SWMMResultsLayer;
class SWMMNodePropertyAdapter;
class SWMMLinkPropertyAdapter;
class SWMMSubcatchPropertyAdapter;
class SWMMDataObjectPropertyAdapter;
struct IdentifyResult;

namespace openswmmvis
{
class OutputStatsRegistry;
} // Slice QA.2

/*!
 * \class PropertiesPanel
 * \brief A dock widget that displays and edits object attributes using QPropertyModel.
 * \details The panel shows:
 *  - The attributes of identified (clicked) features or SWMM elements.
 *  - The properties of the currently selected map layer.
 *
 *  It uses QPropertyModel / QPropertyItemDelegate so any Q_PROPERTY exposed
 *  by a QObject (layer, feature, SWMM element) is automatically displayed
 *  with the correct editor widget (check box, spin box, colour picker, …).
 *
 *  The panel has a layer drop-down so the user can switch between the
 *  properties of any of the layers that returned results in the last
 *  identify operation.
 */
class PropertiesPanel : public QDockWidget
{
    Q_OBJECT

public:
    /**
     * @param[in] parent Optional QWidget owner; panel owns UI, models/delegates and constructed
     *   adapters, not bound project/registry/results layers.
     * @brief Create Properties dock and install application event filter for Return/Enter handling
     *   within property tree.
     * @note Requires live qApp and GUI thread. HAVE_QPROPERTYMODEL builds grouped editable property
     *   tree/custom typed editors; otherwise empty QStandardItemModel fallback, no property
     *   population. No source binding or engine write at construction.
     */
    explicit PropertiesPanel(QWidget *parent = nullptr);
    /**
     * @brief Remove application event filter, then destroy QObject-owned UI/adapters. Requires qApp
     *   still alive.
     * @note No registry flush, engine close or undo/selection changes. Any borrowed engine/project
     *   context must remain valid until adapters and live editors are released.
     */
    ~PropertiesPanel() override;

    // ----- Content -------------------------------------------------------

    /*!
     * \brief Switches the panel to show the Q_PROPERTY attributes of \p object.
     * \param object  Any QObject whose Q_PROPERTYs should be displayed.
     *                Pass nullptr to clear the panel.
     * \param title   Optional title shown in the tab or header.
     * @note Borrow displayed QObject; caller preserves lifetime for property reads/edits, no
     *   generic ownership transfer. Release old mesh adapter unless object is that same adapter,
     *   set title/default Properties, clear identify combo/cache and hide editor/rainfall buttons.
     *   With property library set QObject into model or clear for null, then expand rows. Does not
     *   delete cached spatial/data adapters, rebind project/stats sources or necessarily hide stale
     *   stats combo. Fallback build does not populate/clear its standard model.
     */
    void showObject(QObject *object, const QString &title = {});

    /*! Resolve a mesh cell, vertex or edge selection without requiring a 1D model.
     * @param[in] canvas Optional borrowed canvas used to find mesh layer by encoded source-path
     *   key; not retained by panel.
     * @param[in] ref Typed encoded MeshCell/MeshVertex/MeshEdge selection, requires matching layer
     *   key and current element bounds.
     * @param[in] depthUnit Cell adapter depth display suffix copied, default m; does not convert
     *   geometry/value units.
     * @brief Create owned adapter for first matching mesh source and display it, otherwise clear
     *   visible panel.
     * @note Cell gets canvas undo context, vertex/edge constructors receive layer/index only. Edge
     *   gate currently accepts local indices 0..2 even on quads, so fourth quad edge cannot display
     *   here. A matching layer with invalid element stops search rather than trying later same-key
     *   source. Attribute signal refreshes only current mesh adapter; source destruction clears
     *   visible panel. No engine/mesh write during selection, borrowed mesh lifetime guarded by
     *   adapter behavior.
     */
    void showMeshObject(MapCanvas *canvas, const SWMMObjectRef &ref,
                        const QString &depthUnit = QStringLiteral("m"));

    /*!
     * \brief Shows the attribute results from an identify operation.
     * \param results  One IdentifyResult per layer.
     * @note Copy result list, release mesh adapter, rebuild layer combo with all layerName entries
     *   and show only FIRST feature of selected result. First result selected explicitly; combo
     *   population can also trigger slot. Empty result list clears property model only with library
     *   enabled. Hide dedicated editor/rainfall controls, retain old cached spatial/data adapters
     *   and title until a nonempty row updates it. No feature selection or engine write.
     */
    void showIdentifyResults(const QList<IdentifyResult> &results);

    /*!
     * \brief Shows the editable properties of the given layer.
     * @param[in] layer Optional borrowed layer; null clears property model through showObject but
     *   still uses Layer Properties title.
     * @brief Display live layer QObject properties through generic showObject, with layer name in
     *   dock title.
     * @note Property setters may mutate layer when user edits; no snapshot/copy, undo stack or
     *   source ownership transferred. Fallback build only updates title/control state.
     */
    void showLayerProperties(OpenSWMMVisLayer *layer);

    /*!
     * \brief Clears the panel.
     * @note Release current mesh adapter by deleteLater, clear property model with optional
     *   library, clear identify combo/cache and hide dedicated editor/rainfall controls. Does not
     *   reset title, stats combo/registry/project binding or delete/null cached
     *   node/link/subcatch/data adapters; their connections can still receive signals. No
     *   engine/undo/selection write.
     */
    void clear();

    /*! Slice Z.5.3 — bind to the active project's model layer so
     *  identify results that resolve to a SWMM node can be shown
     *  via a typed `SWMMNodePropertyAdapter` (editable via existing
     *  QPropertyModel auto-delegates).  Pass nullptr to detach.
     * @param[in] layer Optional borrowed RAW SWMM source pointer; must outlive binding and cached
     *   adapters, detach before deletion.
     * @note Equal pointer no-op. Disconnect old attributeChanged only, store source, connect new
     *   attributeChanged/objectRenamed uniquely. Existing old objectRenamed connection is not
     *   removed here. Null schedules node/link/subcatch adapters for deletion and nulls them, but
     *   does not clear model/identify/data adapter/stats state. Nonnull replacement does not
     *   retarget already cached adapters; caller normally follows with new selection binding and
     *   must preserve old engine lifetime meanwhile.
     */
    void setProject(SWMMModelLayer *layer);

    /*! Slice QA.2 — bind the per-project OutputStatsRegistry so the
     *  Stats-source combo above the property tree can populate from
     *  every loaded SWMMResultsLayer. Pass nullptr to hide the combo
     *  (matches today's behaviour for projects with no results loaded
     *  yet). Idempotent — repeated calls re-wire signals safely.
     * @param[in] registry Optional borrowed RAW per-project registry, caller detaches before
     *   destroying; not QPointer-protected.
     * @note Equal pointer no-op. Disconnect all old registry-to-panel signals, store/connect
     *   identitiesChanged, rebuild combo, then set registry on cached node/link/subcatch adapters.
     *   No result ownership/engine edit. Combo refresh may apply selected source before new
     *   registry is forwarded to adapters; caller ensures coherent GUI-thread source lifetimes
     *   throughout. Does not connect registry destruction or force a second value refresh after
     *   final pointer assignment.
     */
    void setStatsRegistry(openswmmvis::OutputStatsRegistry *registry);

    /*! Bind the project's ACTIVE 1D results layer so the panel's post-run
     *  summary rows follow the active run the same way the Attribute
     *  Table's dynamics columns do (SWMMVis calls this on tab activation
     *  and on every activeResultsLayerChanged). Selects the layer's row in
     *  the Stats-source combo — the combo remains a manual override until
     *  the active layer next changes — and re-reads the stat rows whenever
     *  the bound layer's results are (re)opened or finalized, so a re-run
     *  into the same .out refreshes in place. Pass nullptr to fall back to
     *  the "(editing engine)" sentinel.
     * @param[in] layer Optional borrowed active result source kept in QPointer; null or missing
     *   identity selects editing-engine sentinel.
     * @note On pointer change disconnect all old result-to-panel signals and connect
     *   opened/finalized to statistics refresh. Every call, including equal pointer, resolves
     *   stable ID in current registry, selects matching combo row or zero and reapplies stats
     *   source. Overrides any manual choice on each call, not just pointer transitions. Result
     *   destruction clears guarded pointer but does not directly choose sentinel/rebuild combo. No
     *   simulation/engine content change.
     */
    void setActiveResultsLayer(SWMMResultsLayer *layer);

    /*! Slice DA.2 — show a non-spatial Data Object (curve, time series,
     *  pattern, LID control, pollutant, land use, aquifer, snowpack,
     *  control rule, transect, hydrograph group, street, inlet, or
     *  rain gage) using the typed property adapter for its kind.
     *
     *  Bypasses the identify-result flow; called directly when an
     *  Object Browser leaf is clicked. The kind value is an
     *  `SWMMObjectRef::ObjectType` (cast to int to keep the header
     *  forward-declaration-friendly).
     * @param[in] layer Borrowed source with live engine for new adapter. Not assigned to
     *   m_swmmLayer; host must separately setProject consistently.
     * @param[in] objectKind SWMMObjectRef::ObjectType as int: thirteen data kinds plus RainGage
     *   supported, spatial/unknown clears visible model.
     * @param[in] name Required nonempty exact object name; existence not preflighted, no trim or
     *   case normalization.
     * @note Release mesh adapter. With property library, replace previous data adapter by
     *   deleteLater, construct typed owned adapter, bind source, expose live properties and family
     *   editor button; RainGage also has rainfall button and file-vs-series row gating. Invalid
     *   source/name clears visible model/buttons without deleting previous data adapter. Successful
     *   data bind does not clear last identify combo/cache or cached spatial adapters.
     *   Rename/coordinate callbacks route direct layer apply methods, no panel-wide undo command.
     *   Transect/hydrograph/control signals refresh matching/current data view. Without library,
     *   only releaseMeshAdapter occurs. Host preserves source/engine through callbacks/modal
     *   editors.
     */
    void showDataObject(SWMMModelLayer *layer, int objectKind, const QString &name);

public slots:

    /**
     * @param[in] results Borrowed input copied synchronously by showIdentifyResults; one result may
     *   contain many features but only first shown.
     * @brief Forward identify notification to showIdentifyResults without filtering, selection
     *   change or independent mutation.
     */
    void onIdentifyResult(const QList<IdentifyResult> &results);

    /*! Round-4 follow-up 2026-05-12 — refresh the bound adapter if
     *  its object name matches \p name, so a table-side edit is
     *  reflected in the Property Browser.  No-op when no adapter is
     *  bound or the name doesn't match.
     * @param[in] name Exact nonempty edited name; empty no-op, only cached node/link/subcatch
     *   adapter names considered.
     * @note Suppress objectEdited forwarding while refreshing all property-model values for
     *   matching spatial adapter. If cached node/link engine type changed, replay current
     *   identify-combo binding to choose subclass. No data/mesh adapter matching, source mutation,
     *   undo or guaranteed rebind if no identify row remains. Ignores get_type status after
     *   defaulting to cached type. Guard restored to false, not prior nested state.
     */
    void onObjectEditedExternally(const QString &name);

    /*! LINK_OFFSETS mode changed: a bound link's offset rows are labelled
     *  "Inlet/Outlet Offset" vs "… Elevation", and the label is captured when
     *  the adapter is bound, so replay the bind. No-op unless a link is shown.
     * @brief Rebind currently selected identify result when any cached link adapter exists,
     *   rebuilding offset labels from engine mode.
     * @note Missing link/combo or invalid current identify row no-op. Does not change engine
     *   LINK_OFFSETS, adapter offset values or undo history; may rebuild currently selected other
     *   result because cached link pointer does not prove visible view kind.
     */
    void onOffsetModeChanged();

    /*! Re-key the bound adapter and cached identify result after a rename
     *  from any view. Re-keys only; never emits objectEdited.
     * @param[in] oldName Exact old name, matched across ALL cached adapter kinds and all identify
     *   feature Name keys.
     * @param[in] newName Replacement name copied as-is into stored adapters/cache; no rename
     *   validation here.
     * @param[in] kind Ignored namespace hint, implementation unnamed; same-name cached objects of
     *   other types can also rekey.
     * @note Update stored names then refresh property values with scoped suppression if an adapter
     *   touched; cache-only rename does not refresh values. Never writes engine or emits
     *   objectEdited, but does not update dock title/editor button caption. Caller already
     *   performed rename and preserves coherent source ownership.
     */
    void onObjectRenamed(const QString &oldName, const QString &newName, int kind);

signals:

    /*!
     * \brief Generic property-change signal reserved for edit observers.
     * \param object       The source QObject.
     * \param propertyName Q_PROPERTY name.
     * \param oldValue     Previous value.
     * \param newValue     New value.
     * @note Declared generic property-edit signal; current implementation does not emit it. Actual
     *   typed adapter notifications use objectEdited, so consumers must not rely on this signal for
     *   present edit tracking. Borrowed object/value arguments do not establish ownership or
     *   confirmed persistence.
     */
    void propertyChanged(QObject *object, const QString &propertyName, const QVariant &oldValue,
                         const QVariant &newValue);

    /*! Round-4 follow-up 2026-05-12 — emitted after a property-
     *  browser edit so the Attribute Table dock can mirror the
     *  change.  Wired in `SWMMVis`.  Suppressed when the adapter's
     *  `changed()` was triggered by `onObjectEditedExternally`.
     * @param[in] name Current adapter name or successfully layer-applied rename/coordinate name, no
     *   immutable identity/layer pointer included.
     * @note Most adapter changed handlers emit unless suppress flag set; successful
     *   rename/coordinate callback emits directly. Not a universal undo/engine persistence
     *   guarantee, not emitted for suppressed external/stat refresh. Cached adapters can remain
     *   connected after visible view changes, so receiver resolves current project carefully.
     */
    void objectEdited(const QString &name);

    /*! \brief The rain gage "Plot Rainfall…" button was clicked; the main
     *  window opens the shared Rainfall Visualization dialog focused on
     *  \a gageId (the bound gage — empty if no adapter is bound).
     * @param[in] gageId Current cached data adapter name when button clicked, empty without
     *   adapter; no source ownership/lifetime pin.
     * @note Request only. UI shows button for RainGage view, receiver owns
     *   visualization/opening/error handling; emission itself changes no rainfall/selection/project
     *   data.
     */
    void rainfallPlotRequested(const QString &gageId);

private slots:
    /**
     * @param[in] index Zero-based copied identify result; outside range no-op, empty feature list
     *   clears visible property model only.
     * @brief Bind FIRST feature of result as live typed Node/Link/Subcatch adapter when Type/Name
     *   and current project engine permit, otherwise read-only map.
     * @note Ignores result originating-layer identity when choosing m_swmmLayer; host supplies
     *   coherent project. Engine type/virtual/inlet probes choose subclass, with query status
     *   mostly ignored. Replaces only same-family cached adapter by deleteLater, leaving other
     *   families connected. Wire name/coordinate changes to direct layer apply paths, stats
     *   registry, shape/type-dependent row flags, and edit forwarding. No property-library build
     *   leaves model unpopulated but updates title. No user-selection/engine mutation merely from
     *   binding.
     */
    void onLayerComboIndexChanged(int index);

    /*! Slice BM.0-Browse-Edit (2026-05-25) — right-click on a property
     *  row. For `DataObjectRef` rows: builds "Edit <name> in <editor>…"
     *  routed through ComprehensiveEditorRegistry (greyed-out with gap
     *  tooltip when the editor hasn't shipped yet). For
     *  `NodeCompoundEditRef` rows: single "Edit…" launching
     *  NodeCompoundEditDialog. Otherwise: no menu pops.
     * @param[in] pos Tree viewport-local pixel, label click redirected to sibling value column
     *   EditRole.
     * @note Valid control-rule/transect data view gets family editor on any valid value row,
     *   creating fresh delete-on-close window with null undo stack. DataObjectRef row menus offer
     *   registry-known editor; modal pick implemented only Hydrograph/Pattern/TimeSeries, chosen
     *   nonempty value committed, TS additionally saved; curve opens separate nonmodal editor and
     *   does not assign return name. Other mapped families can show enabled action yet current
     *   switch returns without opening. Node/Link compound refs open modal dialogs and commit
     *   refreshed summary EVEN ON CANCEL because pages can write immediately; GroundwaterSources
     *   skipped. Model setData return ignored. Menu/modal source/value index must survive reentry;
     *   no generic undo/rollback guarantee or auto-flush beyond family paths.
     */
    void onTreeContextMenu(const QPoint &pos);

    /*! 2026-05-29 — Header "Open in <Editor>…" button click. Routes the
     *  active data adapter's category through the shared open-for-edit
     *  dispatch (`ComprehensiveEditorRegistry::openForObject`)
     *  so the attribute-panel button and the object-browser leaf
     *  double-click/right-click use the same dispatch table. No-op when
     *  no data adapter is bound or its category has no shipped editor.
     * @brief Dispatch current data category/name through ComprehensiveEditorRegistry using
     *   m_swmmLayer and null undo stack.
     * @note Missing project/data adapter/category/editor no-op. Does not verify cached adapter
     *   belongs to current raw project, transfer ownership or report dispatcher result. Window
     *   reuse/engine writes depend on family callback, not a universal shared-window guarantee.
     */
    void onOpenInEditorClicked();

private:
    /**
     * @brief Schedule guarded current mesh adapter deleteLater and clear tracking pointer.
     * @note No-op absent/destroyed adapter; does not itself clear QPropertyModel, stop cached
     *   nonmesh adapter signals or edit mesh. Caller must replace/clear displayed model before
     *   deferred adapter destruction.
     */
    void releaseMeshAdapter();
    /**
     * @param[in] watched Borrowed QObject being filtered; nonwidget/outside property tree forwarded
     *   to dock base.
     * @param[in] event Required borrowed Qt event, no null guard; Qt retains ownership.
     * @return True after accepting Return/Enter ShortcutOverride within tree descendants; other
     *   cases base result.
     * @note Return/Enter KeyPress queues tree-context focus restore so delegate can commit first,
     *   but does not consume key itself. No direct property write, undo, global shortcut change
     *   outside tree or retained event pointer. Requires live widget/application event loop.
     */
    bool eventFilter(QObject *watched, QEvent *event) override;

    /**
     * @brief Constructor-only owned layout/model/delegate initialization; not idempotent.
     * @note With optional property library register global metatypes/converters and custom ref
     *   editors, install grouped proxy and numeric-step delegate. Otherwise standard empty model,
     *   no custom delegate. Connect combo/menu/editor/rainfall actions; no project binding or
     *   engine content change. Global converter registrations follow their idempotent helper
     *   contracts.
     */
    void setupUi();

    /*! Slice QA.2 — repopulate the Stats-source combo from the bound
     *  registry's current identities(). Called whenever the registry
     *  emits identitiesChanged() AND when the node adapter changes
     *  (so a node selection refreshes the combo state). Preserves the
     *  current selection by stableId when possible.
     * @note Rebuild under QSignalBlocker: null-UUID editing-engine sentinel then current registry
     *   identities with stable IDs/tooltips, preserve previous UUID or fallback zero. Show controls
     *   when ANY cached spatial adapter pointer exists and real identity available, even if that
     *   adapter is not currently visible. Unblock then explicitly apply selection. No result
     *   ownership/engine writes; raw registry must remain alive.
     */
    void refreshStatsSourceCombo();

    /*! Slice QA.2 — push the combo's current selection into every bound
     *  typed adapter (node / link / subcatchment; idempotent; no-op when
     *  no adapter is bound), then
     *  re-read the property values. Edit-forwarding is suppressed for
     *  the duration — a stats-source change is a view change, not a
     *  model edit.
     * @note Missing combo or all spatial adapters no-op. Convert current payload to UUID,
     *   malformed/missing becomes null sentinel rather than no-op. Set source on EVERY cached
     *   node/link/subcatch adapter with edit forwarding suppressed, restore flag false then refresh
     *   every property row under suppression. No engine/undo write, not a change to active results
     *   layer/manual combo identity.
     */
    void applyStatsSourceToAdapter();

    /*! Re-read every property row from the bound adapters without
     *  forwarding objectEdited (the values changed because a RUN
     *  produced new statistics, not because the user edited the model).
     *  Connected to the active results layer's resultsOpened /
     *  resultsFinalized signals.
     * @brief Re-read every displayed property value when optional model exists, suppressing
     *   objectEdited fan-out.
     * @note Does not limit refresh to statistics rows, resolve active result identity, mutate
     *   engine/undo or replace adapter. Guard resets false rather than preserving previous state;
     *   fallback build changes guard only.
     */
    void refreshAdapterStatValues();

    QTreeView *m_treeView = nullptr;
    QComboBox *m_layerCombo = nullptr;
    QComboBox *m_statsSourceCombo = nullptr;    ///< Slice QA.2
    class QLabel *m_statsSourceLabel = nullptr; ///< Slice QA.2
    /*! 2026-05-29 — Header button that opens the active data adapter's
     *  category in its dedicated CRUD editor. Hidden unless a data
     *  adapter is bound to a category with a shipped editor. */
    QPushButton *m_openEditorButton = nullptr;
    /*! Rain gage extra — opens the Rainfall Visualization dialog. Visible
     *  only while a rain gage adapter is bound. */
    QPushButton *m_plotRainGageButton = nullptr;
    /*! 2026-05-29 — DataCategory of the currently shown data adapter,
     *  or `NumDataCategories` when the panel is not showing a data
     *  object (spatial selection, layer properties, identify, or
     *  cleared). Drives `m_openEditorButton` visibility. */
    SWMMModelLayer::DataCategory m_dataObjectCategory = SWMMModelLayer::NumDataCategories;
    QAbstractItemModel *m_model = nullptr;
    QAbstractItemDelegate *m_delegate = nullptr;

    QList<IdentifyResult> m_lastResults;

    // Slice Z.5.3 / AG.3 — engine binding for typed adapters.
    SWMMModelLayer *m_swmmLayer = nullptr;
    SWMMNodePropertyAdapter *m_nodeAdapter = nullptr;
    SWMMLinkPropertyAdapter *m_linkAdapter = nullptr;
    SWMMSubcatchPropertyAdapter *m_subcatchAdapter = nullptr;
    // SWMM_NodeType / SWMM_LinkType the bound adapter subclass was built
    // for, so an external type conversion (right-click "Convert To") can
    // detect the kind changed and rebind the correct subclass instead of
    // refreshing a now-wrong one. -1 when no adapter is bound.
    int m_nodeAdapterKind = -1;
    int m_linkAdapterKind = -1;
    // Slice DA.2 — single cached adapter for the 14 non-spatial Data
    // Object kinds. Replaced on every selection; the previous instance
    // is `deleteLater()`d so the QPropertyModel stops referencing it
    // before the new one binds.
    SWMMDataObjectPropertyAdapter *m_dataAdapter = nullptr;
    QPointer<QObject> m_meshAdapter;

    bool m_suppressEditForward = false; ///< Set during onObjectEditedExternally to break loop.

    /// Slice QA.2 — bound registry; null when no project is active. The
    /// panel never owns the registry (SWMMVisProjectWindow does).
    openswmmvis::OutputStatsRegistry *m_statsRegistry = nullptr;

    /// The project's active 1D results layer (the run the Attribute
    /// Table's dynamics columns read). QPointer — the layer may be
    /// closed/destroyed at any time.
    QPointer<SWMMResultsLayer> m_activeStatsLayer;
};

#endif // PROPERTIESPANEL_H
