/**
 * @file      pollutanteditordialog.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Two-pane CRUD editor for SWMM pollutants ([POLLUTANTS]).
 *
 * Mirrors StreetEditorDialog's role and entry points (createNew /
 * pickPollutant). A pollutant is a flat set of scalar fields, so the layout
 * is a list pane (left) + field form (right); there is no geometry preview.
 *
 *   ┌──────────────┬────────────────────────────────────────┐
 *   │  Pollutants  │  Name                                  │
 *   │  list view   │  Units (combo) / Rain / GW / I&I       │
 *   │  [+ New]     │  Initial conc / Decay / Mol. weight    │
 *   │  [- Delete]  │  Snow only / Co-pollutant / Co-fraction│
 *   └──────────────┴────────────────────────────────────────┘
 *
 * MVC contract — every mutation goes through PollutantProvider; subscribed
 * registry signals keep the list and field form in lock-step. Units are
 * write-once at engine creation (no swmm_pollutant_set_units), so the units
 * combo only takes effect for not-yet-created pollutants.
 *
 * NOTE (first cut): build-verify with the compiler in the loop. See
 * docs/HANDOFF_compile_verify_agent.md.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_POLLUTANTEDITORDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_POLLUTANTEDITORDIALOG_H

#include <QDialog>
#include <QPointer>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QListView;
class QPushButton;
class QSplitter;

class SWMMModelLayer;

namespace openswmmvis::pollutant
{
class PollutantProvider;
class PollutantRegistry;
}

namespace openswmmvis::ui
{

class PollutantListModel;

class PollutantEditorDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Mode { Edit, CreateNew };
    Q_ENUM(Mode)

    /**
     * @brief Build a live provider-backed pollutant editor and select its first entry.
     * @param[in] registry Borrowed nullable registry, guarded through QPointer; null leaves an
     * unbound form.
     * @param[in] layer Borrowed optional model layer, guarded through QPointer;
     * destruction/engine closure invalidates the editor.
     * @param[in] parent Optional Qt owner of the dialog and its controls/models.
     * @details Subscribes to registry renames and context closure, binds the list model and
     * edits providers immediately on the GUI thread. Child widgets/models are owned; providers
     * remain registry-owned. Closing the editor is not a rollback.
     */
    PollutantEditorDialog(openswmmvis::pollutant::PollutantRegistry *registry,
                          SWMMModelLayer *layer, QWidget *parent = nullptr);
    /**
     * @brief Release Qt-owned editor children and signal connections.
     * @details Default destructor does not own providers/registry/layer or perform an explicit
     * save/rollback.
     */
    ~PollutantEditorDialog() override;

    /**
     * @brief Allocate an unshown editor and immediately attempt to create a pollutant provider.
     * @param[in] registry Borrowed nullable registry used for creation; not owned.
     * @param[in] layer Borrowed optional model context; not owned.
     * @param[in] parent Optional Qt parent owning the returned dialog.
     * @return Newly allocated dialog in CreateNew mode, even if no registry/provider could be
     * created. Caller must show/manage/delete it; delete-on-close is not enabled here.
     * @details Creation is immediate rather than staged behind dialog acceptance.
     */
    static PollutantEditorDialog *createNew(openswmmvis::pollutant::PollutantRegistry *registry,
                                            SWMMModelLayer *layer, QWidget *parent = nullptr);

    /**
     * @brief Run a modal edit/create workflow and return its current provider name on close.
     * @param[in] registry Borrowed registry that must remain alive for the entire nested event
     * loop and final save; null returns empty immediately.
     * @param[in] layer Borrowed optional model context, not owned.
     * @param[in] initialName Borrowed identity to preselect when found; empty creates a
     * suggested-name provider immediately. Missing nonempty names retain the constructor's
     * initial selection.
     * @param[in] parent Optional owner for the stack-lifetime child dialog.
     * @return Copied current provider name or empty when none remains; does not depend on
     * accepted versus rejected result.
     * @details Provider edits are live, exec() result is ignored and saveToEngine() is called
     * after closing without inspecting its result. No transaction or cancel rollback is
     * supplied.
     *
     * @details Modal pick / create / edit entry point — mirrors pickStreet. Empty
     *  \p initialName → CreateNew; otherwise → Edit pre-selecting that name.
     *  Returns the bound pollutant name on close.
     */
    static QString pickPollutant(openswmmvis::pollutant::PollutantRegistry *registry,
                                 SWMMModelLayer *layer, const QString &initialName,
                                 QWidget *parent = nullptr);

    /**
     * @brief Report the entry-point mode chosen for this editor.
     * @return Edit by default, or CreateNew when a creation entry point set it; not inferred
     * from current selection.
     */
    Mode mode() const noexcept { return m_mode; }
    /**
     * @brief Borrow the currently bound pollutant provider.
     * @return Registry-owned provider, or null when unbound/destroyed; do not delete or retain
     * beyond its guarded lifetime.
     */
    openswmmvis::pollutant::PollutantProvider *currentProvider() const noexcept;

    // ── Test hooks ──────────────────────────────────────────────────────────
    /**
     * @brief Borrow the dialog's pollutant list widget.
     * @return Dialog-owned initialized QListView; invalid after child/dialog destruction.
     */
    QListView *listView() const noexcept { return m_listView; }
    /**
     * @brief Borrow the dialog's registry-backed list adapter.
     * @return Dialog-owned PollutantListModel; does not transfer provider ownership.
     */
    PollutantListModel *listModel() const noexcept { return m_listModel; }
    /**
     * @brief Borrow the dialog's current-provider name editor.
     * @return Dialog-owned QLineEdit; editingFinished drives the normal validation/rename path.
     */
    QLineEdit *nameEdit() const noexcept { return m_nameEdit; }

    /**
     * @brief Trigger the same immediate creation path as the New button.
     * @details No-ops without a registry; does not show the dialog or change its entry-point
     * mode.
     */
    void invokeNew();

    /**
     * @brief Show/raise the editor and select a requested provider when available.
     * @param[in] name Borrowed name looked up in the registry; empty/missing identity leaves the
     * existing selection unchanged.
     * @details Modeless; does not allocate a new provider or transfer dialog ownership.
     *
     * @details Modeless open-for-edit: shows the dialog and selects \p name.
     *  Used by the Object Browser, Properties panel and Attribute Table.
     */
    void openForPollutant(const QString &name);

public slots:
    /**
     * @brief Clear all mutation targets before disabling and rejecting the editor.
     * @details Disconnects registry-to-dialog signals, clears guarded provider/registry/layer
     * references, unbinds the list/form and rejects. Clearing targets first protects focus-out
     * edits; existing provider/engine changes are not rolled back.
     */
    void invalidateContext();

private slots:
    /**
     * @brief Bind the form to the selected list row, or unbind for an invalid index.
     * @details Reads the list model's provider mapping and performs no registry mutation.
     */
    void onListSelectionChanged_();
    /**
     * @brief Ask the registry for a unique suggested-name pollutant and select it if created.
     * @details No-ops without a registry; registry owns the new provider and its materialization
     * policy. No separate Apply/acceptance gate is used.
     */
    void onAddClicked_();
    /**
     * @brief Confirm and remove the originally selected provider through its registry.
     * @details Captures a guarded victim before the nested confirmation, then rechecks
     * registry/victim membership before deleting. On confirmation selects the first remaining
     * provider or unbinds. Does not add a dialog-local undo command or inspect removal status.
     */
    void onDeleteClicked_();
    /**
     * @brief Validate a trimmed name and request registry rename of the bound provider.
     * @details Suppressed during hydration or without a live target; empty/unchanged names do
     * nothing. Invalid SWMM identifiers or failed rename show warnings and restore the provider
     * name when still applicable; edits otherwise take effect immediately.
     */
    void onNameEdited_();
    /**
     * @brief Commit only the sender control's scalar value to the current provider.
     * @details Suppressed while hydrating or without a provider. Concentrations retain its
     * selected MG/L, UG/L or count/L convention; decay is per day and other values retain
     * provider units. Unknown/no sender writes nothing. Does not rewrite unrelated rounded
     * controls, convert values, check setter success or supply undo. Units change provider
     * metadata but the engine registry's creation-only unit field is not retroactively changed.
     */
    void onFieldEdited_();
    /**
     * @brief Refresh the bound provider's visible name after a registry rename.
     * @param[in] p Borrowed renamed provider; ignored unless it is current.
     * @param[in] prev Previous ID, deliberately unused by this callback.
     * @param[in] now New ID copied into the name widget if it differs.
     * @details Temporarily suppresses field synchronization so external name refresh does not
     * issue another mutation.
     */
    void onProviderRenamed_(openswmmvis::pollutant::PollutantProvider *p, const QString &prev,
                            const QString &now);

private:
    /**
     * @brief Create the owned list/form controls and live edit signal routes once.
     * @details Adds three concentration-unit choices, nonnegative scalar ranges,
     * snow-only/co-pollutant controls and Close actions that accept the dialog. Requires an
     * uninitialized control tree; no provider is created here.
     */
    void buildUi_();
    /**
     * @brief Hydrate or disable the detail form for a borrowed provider.
     * @param[in] p Registry-owned nullable provider stored as a guarded pointer; null clears the
     * name and disables fields/deletion.
     * @details Rebuilds co-pollutant choices excluding p, copies available values into
     * range/precision-limited widgets and suppresses mutation callbacks during hydration. Unit
     * choices remain visible even though existing engine pollutant units are creation-only.
     */
    void bindProvider_(openswmmvis::pollutant::PollutantProvider *p);
    /**
     * @brief Reload None plus registry provider identities except the current pollutant.
     * @param[in] current Borrowed provider excluded by pointer identity; null excludes none.
     * @details Blocks combo signals while replacing copied labels/data, then unconditionally
     * unblocks them; selection is restored separately by bindProvider_().
     */
    void rebuildCoPollutantCombo_(openswmmvis::pollutant::PollutantProvider *current);
    /**
     * @brief Select a registry provider row or bind the supplied provider directly.
     * @param[in] p Borrowed nullable provider; not owned or validated as a registry member on
     * the direct-bind fallback.
     * @details A valid row changes list selection and normally triggers form binding through
     * selectionChanged; absent/null mapping binds immediately.
     */
    void selectProviderInList_(openswmmvis::pollutant::PollutantProvider *p);
    /**
     * @brief Generate an unused PollutantN candidate from registry count plus one.
     * @return Owned candidate, incrementing until registry hasName() is false; Pollutant1 when
     * no registry exists. Does not reserve or create the name.
     */
    QString suggestUniqueName_() const;

    QPointer<openswmmvis::pollutant::PollutantRegistry> m_registry;
    QPointer<SWMMModelLayer> m_layer;
    QPointer<openswmmvis::pollutant::PollutantProvider> m_current;
    Mode m_mode = Mode::Edit;

    QSplitter *m_splitter = nullptr;

    // Left pane.
    QListView *m_listView = nullptr;
    PollutantListModel *m_listModel = nullptr;
    QPushButton *m_addBtn = nullptr;
    QPushButton *m_delBtn = nullptr;

    // Right pane (field form).
    QLineEdit *m_nameEdit = nullptr;
    QComboBox *m_unitsCombo = nullptr;
    QDoubleSpinBox *m_rainSpin = nullptr;
    QDoubleSpinBox *m_gwSpin = nullptr;
    QDoubleSpinBox *m_rdiiSpin = nullptr;
    QDoubleSpinBox *m_initSpin = nullptr;
    QDoubleSpinBox *m_decaySpin = nullptr;
    QDoubleSpinBox *m_mwtSpin = nullptr;
    QCheckBox *m_snowOnlyCheck = nullptr;
    QComboBox *m_coPollCombo = nullptr;
    QDoubleSpinBox *m_coFracSpin = nullptr;

    bool m_suppressFieldSync = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_POLLUTANTEDITORDIALOG_H
