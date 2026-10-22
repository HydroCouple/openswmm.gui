/**
 * @file      aquifereditordialog.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Three-pane CRUD editor for SWMM aquifers ([AQUIFERS]).
 *
 * Phase G2 (workplans/AQUIFER_GROUNDWATER_EXCHANGE_GUI_PLAN_2026-09-05.md).
 *
 * Mirrors LidControlEditorDialog: list pane (add / delete), a grouped form
 * over the twelve AquiferProvider::Param values plus the upper-zone
 * evaporation pattern, and a SectionPreviewWidget showing the two-zone
 * groundwater illustration (buildAquiferDiagram) whose callouts track the
 * form and emphasise the focused field. Soft validation (plan D6) is shown in
 * a status label and forwarded to the diagram; the engine bounds stay the
 * hard limits via the spin-box ranges.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_AQUIFEREDITORDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_AQUIFEREDITORDIALOG_H

#include <QDialog>
#include <QPointer>
#include <QStringList>
#include <QVector>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListView;
class QPushButton;
class QSplitter;

class SWMMModelLayer;

namespace openswmmvis::aquifer
{
class AquiferProvider;
class AquiferRegistry;
}

namespace openswmmvis::sectionview
{
class SectionPreviewWidget;
}

namespace openswmmvis::ui
{

class AquiferListModel;

class AquiferEditorDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Mode { Edit, CreateNew };
    Q_ENUM(Mode)

    /**
     * @brief Build the list, native-unit aquifer form and live two-zone diagram.
     * @param[in] registry Borrowed nullable provider registry, guarded by QPointer; providers
     * remain registry-owned.
     * @param[in] layer Borrowed optional model context, guarded by QPointer; supplies the
     * pattern editor when present.
     * @param[in] parent Optional Qt owner of the dialog and child controls/models.
     * @details Selects the first available provider; uses GUI-thread immediate provider edits
     * and nonblocking relationship warnings. Caller keeps the associated engine context valid
     * through all edits and nested confirmations. Closing the dialog does not roll back
     * provider/registry mutations.
     */
    AquiferEditorDialog(openswmmvis::aquifer::AquiferRegistry *registry, SWMMModelLayer *layer,
                        QWidget *parent = nullptr);
    /**
     * @brief Release owned Qt children without owning registry providers or engine.
     * @details Default destructor adds no explicit save, rollback or undo action.
     */
    ~AquiferEditorDialog() override;

    /**
     * @brief Allocate an unshown CreateNew editor and immediately request a new provider.
     * @param[in] registry Borrowed nullable registry receiving a suggested-name aquifer.
     * @param[in] layer Borrowed optional model context; not owned.
     * @param[in] parent Optional Qt owner of the allocated dialog.
     * @return Newly allocated editor, possibly unbound if creation fails; caller manages
     * showing/deletion and no delete-on-close attribute is added.
     */
    static AquiferEditorDialog *createNew(openswmmvis::aquifer::AquiferRegistry *registry,
                                          SWMMModelLayer *layer, QWidget *parent = nullptr);

    /**
     * @brief Run a modal live aquifer editor and return its selected provider ID on close.
     * @param[in] registry Borrowed registry required to remain alive through exec() and final
     * save; null returns empty immediately.
     * @param[in] layer Borrowed optional model/pattern context; not owned.
     * @param[in] initialName Borrowed identity to preselect when found; empty creates a
     * suggested aquifer immediately, while missing nonempty identity retains initial selection.
     * @param[in] parent Optional Qt owner of the stack-lifetime child dialog.
     * @return Copied current provider name, or empty if none remains, independent of
     * Accepted/Rejected result.
     * @details Ignores exec() result and calls saveToEngine() on closure without checking
     * status. Live provider changes are not canceled or rolled back.
     */
    static QString pickAquifer(openswmmvis::aquifer::AquiferRegistry *registry,
                               SWMMModelLayer *layer, const QString &initialName,
                               QWidget *parent = nullptr);

    /**
     * @brief Report the entry-point mode stored in the dialog.
     * @return Edit by default or CreateNew when explicitly selected by a creation entry point;
     * independent of current selection.
     */
    Mode mode() const noexcept { return m_mode; }
    /**
     * @brief Borrow the guarded current aquifer provider.
     * @return Registry-owned pointer or null when unbound/destroyed; no ownership transfer.
     */
    openswmmvis::aquifer::AquiferProvider *currentProvider() const noexcept;

    // ── Test hooks ──────────────────────────────────────────────────────────
    /**
     * @brief Borrow the initialized aquifer list widget.
     * @return Dialog-owned QListView, valid until child/dialog destruction.
     */
    QListView *listView() const noexcept { return m_listView; }
    /**
     * @brief Borrow the registry-backed aquifer list adapter.
     * @return Dialog-owned AquiferListModel; providers remain registry-owned.
     */
    AquiferListModel *listModel() const noexcept { return m_listModel; }
    /**
     * @brief Borrow the current aquifer's name control.
     * @return Dialog-owned QLineEdit; text edits refresh the diagram and editingFinished drives
     * validated rename.
     */
    QLineEdit *nameEdit() const noexcept { return m_nameEdit; }
    /**
     * @brief Borrow one parameter editor by AquiferProvider ordinal.
     * @param[in] param Zero-based parameter index; negative or at least the current spin-vector
     * size is invalid.
     * @return Dialog-owned numeric editor, or null for an invalid index; values retain native
     * parameter units.
     *
     * @details Spin box for AquiferProvider::Param \p param, or nullptr.
     */
    QDoubleSpinBox *spinBox(int param) const noexcept;
    /**
     * @brief Borrow the optional monthly evaporation-pattern selector.
     * @return Dialog-owned QComboBox; row zero represents no pattern.
     */
    QComboBox *evapPatternCombo() const noexcept { return m_patternCombo; }
    /**
     * @brief Borrow the display of nonblocking parameter-relationship warnings.
     * @return Dialog-owned QLabel; hidden when validationWarnings() is empty.
     */
    QLabel *warningLabel() const noexcept { return m_statusLabel; }
    /**
     * @brief Borrow the owned two-zone illustration widget.
     * @return Dialog-owned SectionPreviewWidget; not an engine solver/state object.
     */
    openswmmvis::sectionview::SectionPreviewWidget *diagram() const noexcept { return m_diagram; }
    /**
     * @brief Compare displayed aquifer values for four nonblocking physical inconsistencies.
     * @return Owned warning strings for wilting point above field capacity, field capacity or
     * upper moisture above porosity, and water table below aquifer bottom. Empty when unbound or
     * these checks pass; not a complete engine-validity guarantee and does not reject edits.
     *
     * @details Plan D6 soft-validation texts for the values currently in the form.
     */
    QStringList validationWarnings() const;

    /**
     * @brief Invoke immediate suggested-name aquifer creation without showing the window.
     * @details No-ops without registry and does not alter the entry-point mode.
     */
    void invokeNew();

    /**
     * @brief Show/raise this modeless editor and select an existing named provider.
     * @param[in] name Borrowed identity; missing/empty identity or registry leaves the selection
     * unchanged.
     * @details Does not create a provider or transfer dialog ownership.
     *
     * @details Modeless open-for-edit: shows the dialog and selects \p name.
     *  Used by the Object Browser, Properties panel and Attribute Table.
     */
    void openForAquifer(const QString &name);

protected:
    /**
     * @brief Refresh diagram focus emphasis before forwarding events to QDialog.
     * @param[in] watched Borrowed QObject delivering the event, passed unchanged to the base
     * filter.
     * @param[in] event Non-null borrowed event; FocusIn/FocusOut refresh the illustration only.
     * @return Base QDialog::eventFilter result; this override does not consume focus events
     * itself or mutate aquifer parameters.
     */
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    /**
     * @brief Bind detail controls/diagram to the selected list provider or null.
     * @details Uses the list adapter's row mapping and performs no creation/removal.
     */
    void onListSelectionChanged_();
    /**
     * @brief Create an unused AquiferN provider through the registry and select it.
     * @details No-ops without registry; a failed creation preserves the previous selection.
     * Provider ownership/materialization belongs to the registry.
     */
    void onAddClicked_();
    /**
     * @brief Confirm impact-aware aquifer deletion and select the first remaining provider.
     * @details Adds the registry's engine-reference impact summary before confirmation. Caller
     * keeps registry/model stable through the nested message box; confirmed deletion routes
     * through the registry with no dialog-local undo or result status.
     */
    void onDeleteClicked_();
    /**
     * @brief Trim/validate the current ID and request registry rename.
     * @details Suppressed during hydration or without live targets. Empty/unchanged text does
     * nothing; invalid SWMM identifiers or failed rename show warnings and restore current
     * provider text.
     */
    void onNameEdited_();
    /**
     * @brief Copy all displayed scalar values and the optional evaporation pattern to the
     * provider.
     * @details Suppressed during hydration or without a provider. Writes every spin's
     * rounded/clamped value on any field edit, then the selected pattern (empty for row zero)
     * and refreshes the diagram. Retains native fractions, length and rainfall-rate units
     * without conversion; relationship warnings do not block writes and no local undo is added.
     */
    void onFieldEdited_();
    /**
     * @brief Open the layer's shared pattern editor and refresh the chosen aquifer pattern.
     * @details Requires a layer/current provider and pattern registry. Rebuilds choices after
     * the modal picker even on empty result, retaining the previous identity then invoking the
     * normal all-field edit path; can create/edit shared patterns independently of aquifer
     * values.
     */
    void onPatternPickClicked_();
    /**
     * @brief Refresh name text and the illustration after the current provider is renamed.
     * @param[in] p Borrowed renamed provider, acted on only if current.
     * @param[in] prev Previous identity, intentionally unused by the implementation.
     * @param[in] now New identity copied into the editor if it differs.
     * @details Suppresses field callbacks during text synchronization to prevent feedback edits.
     */
    void onProviderRenamed_(openswmmvis::aquifer::AquiferProvider *p, const QString &prev,
                            const QString &now);

private:
    /**
     * @brief Create the owned list, twelve parameter editors, pattern controls and diagram once.
     * @details Uses four-decimal widget precision and configured per-field ranges.
     * Length/elevation labels use current UnitSystem (ft fallback), conductivity/deep-loss rates
     * use in/hr or mm/hr, tension uses in or mm and fractions/slopes retain their stored
     * conventions. Labels do not convert provider numbers. Requires uninitialized control
     * pointers.
     */
    void buildUi_();
    /**
     * @brief Hydrate one guarded provider and reset diagram extents for the new subject.
     * @param[in] p Borrowed nullable provider stored through QPointer; null disables
     * scalar/pattern editors and clears the illustration.
     * @details Suppresses mutation callbacks while copying display values and pattern choices,
     * refreshes warnings/diagram and zooms to extents. Pattern-picker availability additionally
     * requires a layer; no scalar provider values are changed here.
     */
    void bindProvider_(openswmmvis::aquifer::AquiferProvider *p);
    /**
     * @brief Select a matching registry row or bind the supplied provider directly.
     * @param[in] p Borrowed nullable provider; not owned and direct fallback does not verify
     * registry membership.
     * @details Normal selection drives detail binding through the list's selectionChanged
     * signal.
     */
    void selectProviderInList_(openswmmvis::aquifer::AquiferProvider *p);
    /**
     * @brief Find an unused AquiferN candidate without reserving it.
     * @return Owned identity beginning at providerCount()+1 and incrementing until unused;
     * Aquifer1 without a registry.
     */
    QString suggestUniqueName_() const;
    /**
     * @brief Rebuild no-pattern plus monthly-pattern choices and preserve a requested identity.
     * @param[in] select Borrowed desired pattern name; empty selects no-pattern, unknown
     * nonempty name is appended visibly rather than silently cleared.
     * @details Uses the registry engine without requiring a layer, skips patterns whose
     * successful type query reports non-monthly, and suppresses dialog mutation callbacks while
     * replacing items. A failed type query does not itself exclude an ID.
     */
    void populatePatternCombo_(const QString &select);
    /**
     * @brief Build the illustration and warning display from current widget values.
     * @details Clears the diagram when unbound, otherwise copies all twelve scalars, name,
     * selected pattern, unit labels and focused-parameter index into buildAquiferDiagram(). Does
     * not write the provider or reset the user's view on ordinary edits; no-ops before diagram
     * creation.
     */
    void refreshDiagram_();
    /**
     * @brief Identify the currently focused parameter for diagram emphasis.
     * @return Focused spin index, UpperEvapFrac when the pattern combo has focus, or -1 when
     * none of those controls has focus.
     */
    int activeParam_() const;

    QPointer<openswmmvis::aquifer::AquiferRegistry> m_registry;
    QPointer<SWMMModelLayer> m_layer;
    QPointer<openswmmvis::aquifer::AquiferProvider> m_current;
    Mode m_mode = Mode::Edit;

    QSplitter *m_splitter = nullptr;

    QListView *m_listView = nullptr;
    AquiferListModel *m_listModel = nullptr;
    QPushButton *m_addBtn = nullptr;
    QPushButton *m_delBtn = nullptr;

    QLineEdit *m_nameEdit = nullptr;
    QVector<QDoubleSpinBox *> m_spins; ///< one per AquiferProvider::Param
    QComboBox *m_patternCombo = nullptr;
    QPushButton *m_patternBtn = nullptr;
    QLabel *m_statusLabel = nullptr;

    openswmmvis::sectionview::SectionPreviewWidget *m_diagram = nullptr;

    bool m_suppressFieldSync = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_AQUIFEREDITORDIALOG_H
