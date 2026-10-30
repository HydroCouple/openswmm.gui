/**
 * @file      lidcontroleditordialog.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     CRUD editor for SWMM LID controls ([LID_CONTROLS]).
 *
 * List pane and property tabs for standard LID controls, with an ordered
 * layer editor for NODE controls. Physical parameters and per-layer pollutant
 * rates/expressions share the current control selection and Apply workflow.
 * Existing definitions are loaded through the registry's engine getters.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_LIDCONTROLEDITORDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_LIDCONTROLEDITORDIALOG_H

#include <QDialog>
#include <QPointer>

class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QListView;
class QPushButton;
class QSplitter;
class QTabWidget;
class QSpinBox;
class QLabel;
class QFormLayout;
class QStandardItemModel;

class SWMMModelLayer;
class QTableView;

namespace openswmmvis::lid
{
class LidControlProvider;
class LidControlRegistry;
}

namespace openswmmvis::sectionview
{
class SectionPreviewWidget;
}

namespace openswmmvis::ui
{

class LidControlListModel;
class LidNodeLayerModel;

class LidControlEditorDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Mode { Edit, CreateNew };
    Q_ENUM(Mode)

    /**
     * @brief Build a live standard-LID editor with a separate ordered NODE-layer draft.
     * @param[in] registry Borrowed nullable registry guarded by QPointer; providers and cached
     * engine remain registry-owned.
     * @param[in] layer Borrowed optional model context guarded by QPointer, used for edited
     * notification.
     * @param[in] parent Optional Qt owner of the dialog and child controls/models.
     * @details GUI-thread editor initially selects the first provider. Standard scalar/type/name
     * changes affect providers immediately; ordered layers, retention, flow numerics and
     * treatment rows remain a local draft until applyLayers_(). Caller keeps registry/engine
     * context valid through operations and nested dialogs; no explicit engine-close invalidation
     * is installed here.
     */
    LidControlEditorDialog(openswmmvis::lid::LidControlRegistry *registry, SWMMModelLayer *layer,
                           QWidget *parent = nullptr);
    /**
     * @brief Destroy Qt-owned controls/models without owning LID providers or engine.
     * @details Default destructor does not call finishLayerDraft_(); interactive completion goes
     * through done() instead.
     */
    ~LidControlEditorDialog() override;

    /**
     * @brief Allocate an unshown CreateNew editor and immediately request a new LID provider.
     * @param[in] registry Borrowed nullable registry used for suggested-name creation.
     * @param[in] layer Borrowed optional model context; not owned.
     * @param[in] parent Optional Qt owner of the allocated dialog.
     * @return Newly allocated editor, possibly unbound if creation fails. Caller manages
     * show/deletion; delete-on-close is not enabled here and creation is not deferred until
     * acceptance.
     */
    static LidControlEditorDialog *createNew(openswmmvis::lid::LidControlRegistry *registry,
                                             SWMMModelLayer *layer, QWidget *parent = nullptr);

    /**
     * @brief Read the editor's entry-point mode flag.
     * @return Edit by default or CreateNew when explicitly set by a creation entry point;
     * independent of selected provider.
     */
    Mode mode() const noexcept { return m_mode; }
    /**
     * @brief Borrow the current guarded LID control provider.
     * @return Registry-owned pointer or null when unbound/destroyed; no ownership transfer.
     */
    openswmmvis::lid::LidControlProvider *currentProvider() const noexcept;

    /**
     * @brief Borrow the initialized LID list widget.
     * @return Dialog-owned QListView, valid until child/dialog destruction.
     */
    QListView *listView() const noexcept { return m_listView; }
    /**
     * @brief Borrow the registry-to-list adapter.
     * @return Dialog-owned LidControlListModel; providers remain registry-owned.
     */
    LidControlListModel *listModel() const noexcept { return m_listModel; }
    /**
     * @brief Borrow the current control's name editor.
     * @return Dialog-owned QLineEdit; editingFinished invokes identifier validation and registry
     * rename.
     */
    QLineEdit *nameEdit() const noexcept { return m_nameEdit; }

    /**
     * @brief Invoke immediate New-button provider creation without showing the dialog.
     * @details No-ops without registry and does not change the entry-point mode. Selection may
     * still be blocked by an existing layer-draft prompt.
     */
    void invokeNew();

    /**
     * @brief Show/raise this modeless editor and request selection of a named control.
     * @param[in] name Borrowed identity; empty, unknown or absent registry preserves current
     * selection.
     * @details A selection change may prompt about a pending NODE draft; no provider is created
     * by this entry point.
     *
     * @details Modeless open-for-edit: shows the dialog and selects \p name.
     *  Used by the Object Browser, Properties panel and Attribute Table.
     */
    void openForLidControl(const QString &name);

protected:
    /**
     * @brief Resolve a pending NODE draft before completing the dialog with the requested
     * result.
     * @param[in] result QDialog result passed unchanged to the base done() only if
     * finishLayerDraft_() succeeds.
     * @details Apply/Discard/Cancel prompting occurs even for a rejected result. Cancel or
     * failed application leaves the dialog open; standard live provider edits are not rolled
     * back.
     */
    void done(int result) override;

private slots:
    /**
     * @brief Resolve the old control's draft before binding the newly selected provider.
     * @details Failed/canceled completion restores the current provider's list selection with
     * selection-model signals blocked. Otherwise binds the valid selected row or null. A new
     * registry entry already created before this prompt is not removed by cancellation.
     */
    void onListSelectionChanged_();
    /**
     * @brief Create a unique LIDN provider and request its list selection.
     * @details No-ops without registry or failed creation. Registry owns/materializes the entry;
     * any old NODE draft is handled by the selection-change path rather than deferred creation.
     */
    void onAddClicked_();
    /**
     * @brief Confirm and remove the current provider through the registry.
     * @details Requires live registry/provider and stable context through the nested
     * confirmation; refusal is a no-op. Clears current before removal and selects the first
     * survivor or unbinds. Does not call finishLayerDraft_() or supply dialog-local deletion
     * undo.
     */
    void onDeleteClicked_();
    /**
     * @brief Trim/validate a SWMM identifier and request current-provider rename.
     * @details Suppressed during hydration or without registry/provider. Empty/unchanged names
     * do nothing; invalid syntax or failed rename warns and restores current provider text. Does
     * not itself apply an ordered-layer draft.
     */
    void onNameEdited_();
    /**
     * @brief Commit standard scalar/type controls immediately, or reveal the NODE draft page.
     * @details Suppressed during hydration/unbound state. Sets provider type from the combo,
     * toggles standard versus NODE tabs, then for standard types copies all fifteen scalar
     * controls in stored input units and refreshes the diagram. Depth/thickness use inches or
     * mm, conductivity/seepage inches/hour or mm/hour, slope percent, soil moisture fractions
     * and storage void-to-solid ratio; drain coefficient retains its exponent-dependent input
     * convention. Values come from rounded/clamped widgets without conversion; NODE returns
     * after page/diagram update rather than applying ordered layers.
     */
    void onFieldEdited_();
    /**
     * @brief Synchronize name text after the currently bound provider is renamed.
     * @param[in] p Borrowed renamed provider; ignored unless current.
     * @param[in] prev Previous identity, deliberately unused in the definition.
     * @param[in] now New identity copied into the name widget if different.
     * @details Temporarily suppresses field callbacks; does not issue another registry rename or
     * explicitly refresh the diagram.
     */
    void onProviderRenamed_(openswmmvis::lid::LidControlProvider *p, const QString &prev,
                            const QString &now);

private:
    /**
     * @brief Create owned standard tabs, NODE-layer/treatment draft controls and diagrams once.
     * @details Builds native-unit scalar editors, ordered physical/retention tables, Richards
     * numerical controls, treatment rate/expression delegates and Apply-layer actions. Richards
     * alpha/specific storage use 1/m and maximum step seconds; layer depths/rates use inch-or-mm
     * authoring conventions. Treatment removal is percent and decay per day. Connects draft
     * dirtiness separately from immediate scalar edits; requires uninitialized control pointers.
     */
    void buildUi_();
    /**
     * @brief Hydrate a guarded provider and replace the local layer/retention/treatment draft.
     * @param[in] p Borrowed nullable registry-owned control; null clears the name and disables
     * detail controls.
     * @details Suppresses field synchronization while copying standard widgets, ordered rows,
     * compatible retention arrays, flow settings and treatment data. Existing engine controls
     * keep their type (combo disabled); unmaterialized controls may change type. Clears
     * layer-draft dirty state, refreshes treatment/diagram and resets illustration extents.
     * Caller must finish the old draft first when preservation is required.
     */
    void bindProvider_(openswmmvis::lid::LidControlProvider *p);

    /**
     * @brief Build the LID illustration from the current widgets or ordered draft rows.
     * @details Clears the diagram when unbound. NODE uses all physical draft layers except
     * BOTTOM, maps their thickness/porosity/conductivity and labels depth in in/mm. Standard
     * types copy current scalar values without conversion and use the available UnitSystem
     * length label. Surface/Soil/Storage tabs request matching highlight; other tabs leave
     * explicit highlight unset. Does not commit provider/engine state or reset view extents.
     *
     * @details Slice SP.6 — rebuild the layer-stack diagram from the CURRENT widget
     *  values and the active tab. Called on every field edit, type change and
     *  tab switch, so the drawing always matches the form.
     */
    void refreshLayerDiagram_();
    /**
     * @brief Request a matching registry row or bind the supplied provider directly.
     * @param[in] p Borrowed nullable provider; not owned and direct fallback does not verify
     * registry membership.
     * @details Normal selection invokes onListSelectionChanged_(), including old-draft
     * prompting. Null or absent row binds immediately without that prompt, so callers preserve
     * drafts explicitly when needed.
     */
    void selectProviderInList_(openswmmvis::lid::LidControlProvider *p);
    /**
     * @brief Find an unused LIDN identity without creating or reserving it.
     * @return Owned candidate beginning at providerCount()+1 and incrementing until unused; LID1
     * without registry.
     */
    QString suggestUniqueName_() const;
    /**
     * @brief Create a NODE provider seeded with SURFACE, MEDIA, AGGREGATE and BOTTOM rows.
     * @details Requires registry; creates an immediate suggested-name provider, sets type eight
     * and supplies unit-specific default ordered layers from LidNodeLayerModel before requesting
     * selection. Does not itself call the engine NODE configuration transaction or undo creation
     * if selection is canceled.
     */
    void addLayeredControl_();
    /**
     * @brief Hydrate visible selected-layer controls and retention curves without draft feedback
     * writes.
     * @details Guards m_syncLayerFields, updates physical-layer count and shows fields permitted
     * by row flags/current flow formulation. Adds in/mm depth and in/hr or mm/hr conductivity
     * labels; Richards shows porous-layer retention inputs/curves and hides inactive legacy
     * fields. Copies cached material/porosity to the schematic only; no engine edit or model
     * setData occurs.
     */
    void refreshLayerFields_();
    /**
     * @brief Rebuild the selected physical layer's treatment table from the local model draft.
     * @details Suppresses treatment feedback, clears old rows, disables treatment for
     * invalid/BOTTOM selection and copies read-only pollutant IDs plus 15-significant-digit
     * removal/decay strings and expressions. Updates guidance without changing treatment rules
     * or engine state.
     */
    void refreshTreatmentRows_();
    /**
     * @brief Replace the selected layer's local treatment rules from the editable table.
     * @details No-ops during hydration; otherwise marks the layer draft dirty even if no valid
     * row is selected. For a valid row, copies pollutant/expression text and one-based layer
     * identity, with removal percent and decay per day. Failed numeric conversions are retained
     * as NaN so application can reject them; no engine write occurs.
     */
    void storeTreatmentRows_();
    /**
     * @brief Validate/install the complete NODE stack, treatment and flow/retention
     * configuration.
     * @return False without live provider/registry/engine, on creation failure or native
     * configuration refusal; true after engine installation and provider synchronization.
     * @details Creates a missing type-eight engine control, resolves pollutant IDs/temporary
     * UTF-8 expressions and calls swmm_lid_node_configure_flow() with the entire draft. Native
     * validation controls stack/order/material/outlet eligibility. On refusal, best-effort
     * deletes a newly created control and shows an explanatory warning; does not supply undo or
     * a broader transaction around creation. Success copies treatment/layers/flow back to
     * provider, clears provider/draft dirty flags and marks the optional model edited. Existing
     * assignments/anchored outlets follow native configuration semantics.
     */
    bool applyLayers_();
    /**
     * @brief Resolve an outstanding dirty NODE draft before switching subject or closing.
     * @return True for no applicable draft, Discard, or successful Apply; false for Cancel or
     * failed applyLayers_().
     * @details Prompts with Apply/Discard/Cancel only for a bound type-eight dirty draft.
     * Discard clears the dirty flag without immediately restoring displayed local rows; the
     * following bind/close consumes them. Does not undo standard live edits.
     */
    bool finishLayerDraft_();
    bool m_layerDraftDirty = false;

    QPointer<openswmmvis::lid::LidControlRegistry> m_registry;
    QPointer<SWMMModelLayer> m_layer;
    QPointer<openswmmvis::lid::LidControlProvider> m_current;
    QTableView *m_nodeLayerTable = nullptr;
    LidNodeLayerModel *m_nodeLayerModel = nullptr;
    QWidget *m_nodeLayerPage = nullptr;
    QSpinBox *m_mediaCount = nullptr;
    QLabel *m_layerSummary = nullptr;
    QFormLayout *m_layerFields = nullptr;
    QFormLayout *m_retentionFields = nullptr;
    QTabWidget *m_layerDetails = nullptr;
    QDoubleSpinBox *m_layerValues[14]{};
    QComboBox *m_flowModel = nullptr;
    QWidget *m_richardsSettings = nullptr;
    QLabel *m_richardsBoundaryNotice = nullptr;
    QSpinBox *m_richardsCells = nullptr;
    QDoubleSpinBox *m_richardsAtol = nullptr, *m_richardsRtol = nullptr,
                   *m_richardsMaxStep = nullptr;
    QWidget *m_retentionCurves = nullptr;
    bool m_syncLayerFields = false;
    QTableView *m_treatmentTable = nullptr;
    QStandardItemModel *m_treatmentModel = nullptr;
    QLabel *m_treatmentMessage = nullptr;
    bool m_syncTreatment = false;
    Mode m_mode = Mode::Edit;

    QSplitter *m_splitter = nullptr;

    /*! Slice SP.6 — right-hand layer-stack drawing: which layers this LID type
     *  has, their relative thicknesses, and the active tab highlighted. */
    openswmmvis::sectionview::SectionPreviewWidget *m_diagram = nullptr;
    QTabWidget *m_tabs = nullptr;

    QListView *m_listView = nullptr;
    LidControlListModel *m_listModel = nullptr;
    QPushButton *m_addBtn = nullptr;
    QPushButton *m_delBtn = nullptr;

    QLineEdit *m_nameEdit = nullptr;
    QComboBox *m_typeCombo = nullptr;

    // Surface.
    QDoubleSpinBox *m_surfStorage = nullptr, *m_surfRough = nullptr, *m_surfSlope = nullptr;
    // Soil.
    QDoubleSpinBox *m_soilThick = nullptr, *m_soilPoro = nullptr, *m_soilFc = nullptr,
                   *m_soilWp = nullptr, *m_soilKsat = nullptr, *m_soilKslope = nullptr;
    // Storage.
    QDoubleSpinBox *m_storThick = nullptr, *m_storVoid = nullptr, *m_storKsat = nullptr;
    // Drain.
    QDoubleSpinBox *m_drainCoeff = nullptr, *m_drainExpon = nullptr, *m_drainOffset = nullptr;

    bool m_suppressFieldSync = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_LIDCONTROLEDITORDIALOG_H
