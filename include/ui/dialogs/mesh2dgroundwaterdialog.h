/**
 * @file      mesh2dgroundwaterdialog.h
 * @brief     GG1-GG4, GG6 (2026-09-07) — the 2D two-zone groundwater editor.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * GG1-GG4, GG6 (2026-09-07) — the 2D two-zone groundwater editor.
 *
 * This was a display-only preview laid out against a draft design. The kernel
 * now exists (`openswmm.engine/src/engine/2d/subsurface/`), so the dialog is
 * live and talks to it through `openswmm_gw2d.h`:
 *
 *   - **Options** — `[2D_AQUIFER_OPTIONS]`: soil law, closure, σ layers,
 *     stability factors, Dunne and GW_ET.
 *   - **Aquifer** — the `[2D_AQUIFER]` rows, per scope, in a table.
 *   - **Node beds** — `[2D_AQUIFER_NODE]`: which node exchanges with which
 *     cell, through what conductance.
 *   - **State** — read-only, live during a run: saturated thickness, storage,
 *     the closure each cell resolved to, the LTS tier histogram and the
 *     continuity residual.
 *
 * MVC per CLAUDE.md §5.1: the tables are `Mesh2DAquiferModel` and
 * `Mesh2DAquiferNodeModel`, so the same rows can be edited from a future
 * cell-properties panel and stay in step. The dialog owns presentation and
 * the Apply/Cancel contract, nothing else.
 *
 * Editing is refused mid-run by the C API, on purpose: these rows seed the
 * kernel at initialize, so a mid-run edit would be silently dropped. The
 * dialog reads that state up front and disables the editors with a reason
 * rather than letting Apply fail.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_MESH2DGROUNDWATERDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_MESH2DGROUNDWATERDIALOG_H

#include <QDialog>
#include <QMap>
#include <QPointer>
#include <QStringList>
#include <functional>
#include <vector>

#include <openswmm/engine/openswmm_engine.h>

class QUndoStack;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QDialogButtonBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTableView;
class QTabWidget;

class UnitSystem; // core/unitsystem.h — global scope, as every other dialog declares it

namespace openswmmvis::ui
{

class Mesh2DAquiferModel;
class Mesh2DAquiferNodeModel;
class Mesh2DAquiferBoundaryModel;

class Mesh2DGroundwaterDialog : public QDialog
{
    Q_OBJECT

public:
    /*! \brief Which page opens first. */
    enum class Page {
        AquiferProperties,
        InitialConditions,
        Options,
        State,
        NodeBeds,
        EdgeBoundaries
    };
    /**
     * @brief Open Node Beds and select matching cached LID/bed rows when available.
     * @param[in] node Borrowed exact node identity; unknown names leave existing combo/table
     * selections unchanged.
     * @details Always selects tab index two and refreshes the connection summary; no live engine
     * mutation or node creation.
     */
    void focusNode(const QString &node);
    /**
     * @brief Configure optional undo ownership for future node-bed and boundary commits.
     * @param[in] stack Borrowed optional QUndoStack stored through QPointer; null omits those
     * undo commands.
     * @param[in] current Owned callback moved into the dialog and later copied into commands;
     * must return whether their captured engine/model context remains safe before replay. A
     * missing callback makes later replay refuse.
     * @param[in] edited Optional owned GUI-thread callback copied into commands and invoked
     * after successful undo/later redo; must not throw or require this dialog to survive.
     * @details Does not change existing commands or provide undo for aquifer rows/options. A
     * successful Apply with both node and edge changes pushes two separate commands, not one
     * all-settings transaction.
     */
    void setNodeUndoStack(QUndoStack *, std::function<bool()> current,
                          std::function<void()> edited);

    /**
     * @brief Build/load staged groundwater authoring pages and a read-only live-state view.
     * @param[in] engine Borrowed nullable engine; must remain valid until invalidateContext() or
     * dialog destruction, and no ownership is taken.
     * @param[in] parent Optional Qt owner; its destruction invalidates context, and
     * engineAboutToClose is connected when that signal exists on the parent. Other owners must
     * invalidate before engine release.
     * @param[in] initialPage Page to select after UI construction; InitialConditions shares the
     * Aquifer page.
     * @param[in] units Borrowed optional unit system for project labels; unit
     * changes/destruction invalidate the editor, and null uses metre-based labels.
     * @details GUI-thread engine/widget access only. Captures porous-thermal API support, loads
     * exact raw versus displayed option baselines and staged table models, then gates editing to
     * BUILDING/OPENED state without probing setters. Constructor does not write authored engine
     * settings.
     */
    explicit Mesh2DGroundwaterDialog(SWMM_Engine engine, QWidget *parent = nullptr,
                                     Page initialPage = Page::AquiferProperties,
                                     const UnitSystem *units = nullptr);

    /**
     * @brief Copy soil-law labels in the aquifer model's engine enum order.
     * @return RUSSO, GARDNER, BROOKS_COREY and VAN_GENUCHTEN via
     * Mesh2DAquiferModel::soilTokens().
     *
     * @details Soil characteristic laws, in the engine's enum order.
     */
    [[nodiscard]] static QStringList soilModelTokens();
    /**
     * @brief Copy closure labels with AUTO first.
     * @return AUTO, CLOSED_FORM, ENSLAVED and SIGMA in combo-index order via Mesh2DAquiferModel.
     *
     * @details Unsaturated-zone closures, AUTO first.
     */
    [[nodiscard]] static QStringList closureTokens();
    /**
     * @brief Enumerate the groundwater and transport option keys represented by this editor.
     * @return Owned fixed-order list of ten hydraulic/process keys and nineteen
     * GW_TRANSPORT:-prefixed keys, including thermal boundary strings. Does not query engine
     * support or return option values.
     */
    [[nodiscard]] static QStringList configurationOptionKeys();
    /**
     * @brief Restrict visible pages to Options and Transport/Thermal, or restore other pages.
     * @param[in] enabled True hides Aquifer, Node Beds, State and Edge Boundaries tabs; false
     * shows them.
     * @details Presentation only; does not remove models, discard drafts, select a thermal model
     * or limit applyChanges() to a subset of stored options. No-ops before tabs exist.
     */
    void setThermalOptionsOnly(bool enabled);

public slots:
    /**
     * @brief Drop borrowed engine access and reject the disabled editor.
     * @details Clears engine/editability before disabling widgets and directly calling
     * QDialog::reject(); does not roll back previous Apply operations.
     */
    void invalidateContext();
    /**
     * @brief Refresh the selected cached LID bottom mode, footprint actions and summary.
     * @details No-ops before bottom controls exist. Blocks destination-combo signals while
     * selecting cached mode, enables destination/footprint edits only for editable known LID
     * nodes and keeps node choice available for read-only inspection. Uses staged/cached model
     * state without engine writes.
     */
    void updateBottomConnection();

signals:
    /**
     * @brief Warn observers conservatively just before authoring publication may begin.
     * @details Emitted after initial validation and before the first write, allowing dirty-state
     * preparation. Synchronous observers may close/change context, so the method rechecks
     * lifetime/engine/baselines afterward. Emission can be followed by refusal with no write, or
     * by a partial write/failed restoration; it does not prove success.
     *
     * @details Authoring writes are about to start; a later refusal may be partial.
     */
    void changesMayHaveBeenApplied();
    /**
     * @brief Notify completion of a changed groundwater Apply after all writes succeed.
     * @details Emitted after baselines are accepted and state refreshed; a clean no-op Apply
     * returns success without this signal. Node/boundary undo callbacks notify through the
     * configured edited callback instead.
     *
     * @details Emitted after all groundwater edits have been applied successfully.
     */
    void applied();

private slots:
    /**
     * @brief Run applyChanges() from the Apply button and leave the dialog open.
     * @details Uses that helper's validation/error reporting and ignores its boolean return; OK
     * separately accepts only after a true result.
     */
    void onApply();
    /**
     * @brief Append/select/scroll to a default local aquifer-property row.
     * @details Assumes initialized model/view and is normally gated by disabled UI when
     * read-only. Does not independently check m_editable or write the engine.
     */
    void onAddAquiferRow();
    /**
     * @brief Remove selected local aquifer rows in descending order.
     * @details Collects selectedRows(), sorts to avoid index shifts and delegates each removal
     * to the staged model. No engine publication until Apply; no independent editability check
     * here.
     */
    void onRemoveAquiferRow();
    /**
     * @brief Append a placeholder node-bed draft and begin editing its node ID.
     * @details Uses literal translated `<node>` and zero-based cell zero, selects the row and
     * opens its name editor. No engine write or independent editability gate.
     */
    void onAddNodeBed();
    /**
     * @brief Remove selected local node-bed rows in descending order.
     * @details Delegates staged erasure after sorting row numbers; actual expected-state
     * validation/publication occurs on Apply.
     */
    void onRemoveNodeBed();
    /**
     * @brief Read current groundwater runtime diagnostics into the text view without advancing
     * simulation.
     * @details Missing view is a no-op; absent engine or inactive kernel displays a status
     * explanation. Reports dimensions, continuity/storage/water ledgers in SI m³, cumulative
     * requested/accepted edge volumes, native-concentration-times-m³ species terms, LTS firings
     * and saturated-thickness/closure census in metres. Failed optional reads omit sections; no
     * authoring/runtime values are changed and no claim of a complete diagnostic dump is made.
     */
    void refreshState();

private:
    /**
     * @brief Create all owned pages, buttons and the initial page selection once.
     * @param[in] initialPage Options, Aquifer/InitialConditions, NodeBeds, State or
     * EdgeBoundaries selection; default maps to Aquifer.
     * @details Builds hydraulic options, aquifer/node models, state text, transport/thermal
     * controls and boundary model. OK calls applyChanges() before accept; Apply keeps the dialog
     * open and Cancel rejects the current draft. Earlier successful Applies are not undone by
     * Cancel. Requires uninitialized UI/model pointers.
     */
    void buildUi(Page initialPage);
    /**
     * @brief Build owned hydraulic default, stability, mesh-mode and ET draft controls.
     * @return Page widget initially parented to this dialog and adopted by the tab widget;
     * caller does not delete it independently.
     * @details Displays soil/closure tokens, 2..128 sigma layers, dimensionless safety factors
     * and process-choice guidance. Callbacks refresh authored/effective ET explanation without
     * publishing settings.
     */
    QWidget *buildOptionsPage();
    /**
     * @brief Build the owned staged aquifer-row model, enum delegates and row actions.
     * @return Dialog-owned/tab-adopted page widget.
     * @details Scope/soil/closure delegates use model-defined ordinals and optional inheritance
     * entries. Values/initial saturated thickness remain project-native; adding/removing rows
     * affects local state until Apply.
     */
    QWidget *buildAquiferPage();
    /**
     * @brief Build node-bed rows and storage-LID bottom/contact-area review controls.
     * @return Owned page adopted by the tab widget.
     * @details Wires local bottom-mode/footprint edits, cached summaries and selected-node
     * synchronization. The native expected-state transaction executes only through Apply, not
     * these draft controls.
     */
    QWidget *buildNodeBedPage();
    /**
     * @brief Build the read-only monospaced runtime diagnostic view and Refresh action.
     * @return Owned page adopted by tabs; no engine query/advance occurs merely from building
     * the controls.
     */
    QWidget *buildStatePage();
    /**
     * @brief Build owned transport, porous-soil heat and thermal-boundary draft controls.
     * @return Scroll area owned by this dialog and adopted by tabs; it owns its
     * content/children.
     * @details Shows species/age/temperature flags, bulk/layered chemistry, diffusion stability
     * and thermal model/conduction choices. Thermal numeric units are seconds, W/m/K, m² K/W,
     * Celsius, W/m² and metre boundary depth regardless of project length units. Connections
     * gate supported/active controls and retain inactive choices without enabling parent
     * processes or writing engine state.
     */
    QWidget *buildTransportPage();
    /**
     * @brief Build the owned staged water/chemistry edge table and load current records.
     * @return Page adopted by the tab widget, owning its views/actions; model is dialog-owned.
     * @details Connects water/species placeholder insertion and selected-row removal. Boundary
     * values use their mixed API conventions (HEAD project elevation, FLOW m³/s inward and
     * species-native terms), with validation/publication deferred to Apply.
     */
    QWidget *buildBoundaryPage();
    Mesh2DAquiferBoundaryModel *m_boundaries = nullptr;
    QWidget *m_boundaryPage = nullptr;
    QMap<QString, QWidget *> m_transportOptions;
    std::vector<QWidget *> m_thermalFields;

    /**
     * @brief Hydrate exact/raw and displayed option baselines plus staged authoring models.
     * @details Null engine disables editing and refreshes explanatory state. Otherwise chooses
     * project length/rate/area labels, copies option controls/thermal strings, loads
     * aquifer/node rows and cached LID choices, refreshes state and gates editing by lifecycle.
     * Separate displayed snapshots preserve originally loaded precision/tokens when unrelated
     * fields change. Does not call authoring setters; partial/missing option reads follow
     * optionText()'s empty-string fallback.
     */
    void loadFromEngine();
    /**
     * @brief Serialize initialized option controls into their native authoring token strings.
     * @return Owned QMap of hydraulic keys and GW_TRANSPORT:-prefixed settings, numeric controls
     * at seventeen significant digits, booleans as YES/NO and assembled thermal
     * mode/value/optional DEPTH tokens.
     * @details Trims free text and appends boundary arguments only for applicable modes; does
     * not validate referenced series or convert physical units. Reads disabled/hidden controls
     * too without touching engine state.
     */
    QMap<QString, QString> optionDraft() const;
    /**
     * @brief Read current engine strings for every key represented by optionDraft().
     * @return Owned key/value map; optionText() returns empty for failed reads and uses its
     * fixed 128-byte buffer. Does not publish setters, validate string completeness or change
     * draft controls.
     */
    QMap<QString, QString> readOptions() const;
    QMap<QString, QString> m_loadedOptions, m_displayedOptions;
    /**
     * @brief Publish only controls whose serialized draft differs from the displayed baseline.
     * @return Empty when all changed setters succeed, otherwise the first key/value refusal
     * text.
     * @details Iterates QMap key order, including GW_TRANSPORT dispatch; no rollback occurs
     * inside this helper, so earlier setters may already have changed authoring flags/values.
     * Caller applyChanges() coordinates restoration against raw loaded options.
     */
    QString applyOptions();
    /**
     * @brief Validate coordinated option/table drafts, publish them and attempt restoration on
     * failure.
     * @return True for clean no-op or complete changed success; false for
     * unavailable/read-only/running context, validation/staleness error, refused write or failed
     * restoration, with visible warnings.
     * @details Commits pending widget edits, requires SIGMA for layered chemistry and exact
     * current option/row/geometry baselines, and validates changed options on a temporary
     * engine. Emits changesMayHaveBeenApplied(), then rechecks lifetime/context and concurrency
     * before writes. Publishes options, aquifer rows, node beds and changed boundaries
     * sequentially; failure tries to restore changed groups/options, reporting incomplete
     * rollback while retaining drafts. Success optionally pushes separate node/boundary undo
     * commands (not option/aquifer undo), accepts all baselines, refreshes state and emits
     * applied(). Does not make the entire multi-group operation atomic.
     */
    bool applyChanges();
    /**
     * @brief Update edit gates, table edit triggers and explanatory banner.
     * @param[in] on True permits draft UI edits; false disables authoring controls and replaces
     * standard buttons with Close.
     * @param[in] whyNot Copied explanation shown in the banner when nonempty; empty hides the
     * banner.
     * @details Keeps runtime Refresh available with a live engine, updates cached LID bottom
     * actions and boundary-model editability, then refines thermal-support/activation gates.
     * Does not query/modify engine lifecycle or automatically restore the previous button set
     * when re-enabled.
     */
    void setEditable(bool on, const QString &whyNot);
    /**
     * @brief Refresh porous-thermal control availability and saved-versus-active explanations.
     * @details Requires constructed model/status controls. Considers local editability, probed
     * engine API support, porous versus water-only model, diffusion/dispersion relevance and
     * boundary argument modes. Reads GROUNDWATER/heat state and temperature transport to explain
     * inactive saved settings; parent-disabled processes are not activated. Preserves all values
     * and writes no engine settings; physical coverage/material validity remains a run-time
     * prerequisite.
     */
    void refreshThermalGates();

    QPointer<QUndoStack> m_nodeUndo;
    std::function<bool()> m_undoCurrent;
    std::function<void()> m_undoEdited;
    SWMM_Engine m_engine = nullptr;
    const UnitSystem *m_units = nullptr;

    QDialogButtonBox *m_buttons = nullptr;
    bool m_editable = false;
    bool m_porousThermalOptionsSupported = false;

    QTabWidget *m_tabs = nullptr;
    QLabel *m_banner = nullptr;
    QLabel *m_thermalStatus = nullptr;

    // Options
    QComboBox *m_soilCombo = nullptr;
    QComboBox *m_closureCombo = nullptr;
    QSpinBox *m_layersSpin = nullptr;
    QCheckBox *m_capillaryCheck = nullptr;
    QDoubleSpinBox *m_cgwSpin = nullptr;
    QDoubleSpinBox *m_ccolSpin = nullptr;
    QCheckBox *m_forceCfCheck = nullptr;
    QCheckBox *m_dunneCheck = nullptr;
    QComboBox *m_modeCombo = nullptr;
    QComboBox *m_gwEtCombo = nullptr;
    QComboBox *m_surfaceThermalKind = nullptr;
    QLineEdit *m_surfaceThermalValue = nullptr;
    QComboBox *m_deepThermalKind = nullptr;
    QLineEdit *m_deepThermalValue = nullptr;
    QDoubleSpinBox *m_deepThermalDepth = nullptr;

    // Tables
    QComboBox *m_bottomNode = nullptr;
    QComboBox *m_bottomDestination = nullptr;
    QLabel *m_bottomSummary = nullptr;
    QPushButton *m_useFootprint = nullptr;
    QTableView *m_aquiferView = nullptr;
    Mesh2DAquiferModel *m_aquifer = nullptr;
    QTableView *m_nodeView = nullptr;
    Mesh2DAquiferNodeModel *m_nodes = nullptr;
    QPushButton *m_addRowBtn = nullptr;
    QPushButton *m_delRowBtn = nullptr;
    QPushButton *m_addBedBtn = nullptr;
    QPushButton *m_delBedBtn = nullptr;

    // State
    QPlainTextEdit *m_stateText = nullptr;
    QPushButton *m_refreshBtn = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_MESH2DGROUNDWATERDIALOG_H
