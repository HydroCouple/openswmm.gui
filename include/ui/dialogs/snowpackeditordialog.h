/**
 * @file      snowpackeditordialog.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Two-pane CRUD editor for SWMM snow packs ([SNOWPACKS]).
 *
 * Mirrors AquiferEditorDialog (list pane + scalar field form). The twenty-seven legacy and five UEB site
 * snow-pack parameters are surfaced as labeled spin boxes, indexed by
 * SnowpackProvider::Param and grouped as PLOWABLE / IMPERVIOUS / PERVIOUS /
 * REMOVAL, plus a line edit for the REMOVAL destination subcatchment.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_SNOWPACKEDITORDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_SNOWPACKEDITORDIALOG_H

#include <QDialog>
#include <QPointer>
#include <QVector>

class QDoubleSpinBox;
class QLineEdit;
class QListView;
class QPushButton;
class QSplitter;
class QComboBox;

class SWMMModelLayer;

namespace openswmmvis::snowpack
{
class SnowpackProvider;
class SnowpackRegistry;
}

namespace openswmmvis::ui
{

class SnowpackListModel;

class SnowpackEditorDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Mode { Edit, CreateNew };
    Q_ENUM(Mode)

    /**
     * @brief Build a shared snowpack provider editor with degree-day and UEB site controls.
     * @param[in] registry Borrowed nullable registry, stored through QPointer; providers remain
     * registry-owned.
     * @param[in] layer Borrowed optional model context, stored through QPointer; not owned.
     * @param[in] parent Optional Qt owner of the dialog and child controls/models.
     * @details Binds the first provider when available and edits provider values immediately on
     * the GUI thread. Caller maintains a live engine context through all operations/nested
     * confirmations; this dialog does not subscribe to engine-close invalidation. Acceptance
     * validates/saves registry state, but rejection does not undo earlier provider or registry
     * changes.
     */
    SnowpackEditorDialog(openswmmvis::snowpack::SnowpackRegistry *registry, SWMMModelLayer *layer,
                         QWidget *parent = nullptr);
    /**
     * @brief Destroy owned Qt controls/models without deleting registry-owned providers.
     * @details Default destructor does not explicitly save or roll back provider edits.
     */
    ~SnowpackEditorDialog() override;

    /**
     * @brief Allocate an unshown CreateNew editor and immediately attempt provider creation.
     * @param[in] registry Borrowed nullable registry receiving the new named provider.
     * @param[in] layer Borrowed optional model context; not owned.
     * @param[in] parent Optional Qt owner of the returned dialog.
     * @return Newly allocated editor, even when no provider could be created; caller manages
     * showing/deletion and no delete-on-close attribute is added.
     * @details New-provider creation is not deferred until acceptance.
     */
    static SnowpackEditorDialog *createNew(openswmmvis::snowpack::SnowpackRegistry *registry,
                                           SWMMModelLayer *layer, QWidget *parent = nullptr);

    /**
     * @brief Run a modal snowpack selector/editor and return only an accepted current identity.
     * @param[in] currentName Borrowed identity to preselect when found; missing/empty leaves the
     * constructor's first-provider selection and does not auto-create a provider.
     * @param[in] parent Optional Qt owner of the stack-lifetime child dialog.
     * @param[in] registry Nullable borrowed registry; null returns empty without opening a
     * dialog. It and its engine must remain valid throughout editing.
     * @param[in] layer Optional borrowed model layer, never owned by this entry point.
     * @return Copied current provider name when exec() returns Accepted and a provider remains,
     * otherwise empty. Acceptance goes through validation/save; cancellation does not roll back
     * already-live provider edits.
     */
    static QString pickSnowpack(openswmmvis::snowpack::SnowpackRegistry *registry,
                                SWMMModelLayer *layer, const QString &currentName,
                                QWidget *parent = nullptr);

    /**
     * @brief Read the editor's entry-point mode flag.
     * @return Edit by default or CreateNew when the creation entry point sets it; independent of
     * selected provider.
     */
    Mode mode() const noexcept { return m_mode; }
    /**
     * @brief Borrow the guarded current snowpack provider.
     * @return Registry-owned pointer or null when unbound/destroyed; do not delete or retain
     * beyond its lifetime.
     */
    openswmmvis::snowpack::SnowpackProvider *currentProvider() const noexcept;

    /**
     * @brief Borrow the initialized snowpack list widget.
     * @return Dialog-owned QListView; valid until its Qt owner destroys it.
     */
    QListView *listView() const noexcept { return m_listView; }
    /**
     * @brief Borrow the registry-backed snowpack list adapter.
     * @return Dialog-owned SnowpackListModel, without provider ownership transfer.
     */
    SnowpackListModel *listModel() const noexcept { return m_listModel; }
    /**
     * @brief Borrow the current-provider name editor.
     * @return Dialog-owned QLineEdit; editingFinished invokes identifier validation and registry
     * rename.
     */
    QLineEdit *nameEdit() const noexcept { return m_nameEdit; }

    /**
     * @brief Trigger the immediate New-button creation route.
     * @details No-ops without registry; does not show the window or change mode().
     */
    void invokeNew();
    /**
     * @brief Save/validate registry state before accepting the dialog.
     * @details A negative saveToEngine() result shows lastSaveError() and keeps the editor open.
     * A nonnegative result, or absent registry, delegates to QDialog::accept(). Does not provide
     * rollback for previous live provider/registry mutations.
     */
    void accept() override;

    /**
     * @brief Show/raise the modeless editor and select an available named provider.
     * @param[in] name Borrowed identity; empty, missing or absent registry leaves current
     * selection unchanged.
     * @details Does not create a provider or transfer dialog ownership.
     *
     * @details Modeless open-for-edit: shows the dialog and selects \p name.
     *  Used by the Object Browser, Properties panel and Attribute Table.
     */
    void openForSnowpack(const QString &name);

private slots:
    /**
     * @brief Bind detail fields to the selected list provider or null for an invalid row.
     * @details Does not itself create/remove providers; bindProvider_() may refresh the
     * provider's default-model metadata.
     */
    void onListSelectionChanged_();
    /**
     * @brief Create an unused SnowpackN provider and select it if the registry accepts it.
     * @details No-ops without registry. Registry owns/materializes the new object; scalar
     * parameters remain provider values until their normal save route.
     */
    void onAddClicked_();
    /**
     * @brief Confirm deletion, remove the current provider and select the first survivor.
     * @details Requires a live registry/current provider and stable context through the nested
     * confirmation. No-op on refusal; an empty registry leaves the form unbound. Does not add
     * dialog-local undo or report removal status.
     */
    void onDeleteClicked_();
    /**
     * @brief Trim/validate the current name and request registry rename.
     * @details Suppressed during hydration or without registry/provider. Empty/unchanged names
     * do nothing; malformed SWMM identifiers or failed rename produce a warning and restore
     * current provider text.
     */
    void onNameEdited_();
    /**
     * @brief Copy all displayed numeric parameters into the current provider on a field edit.
     * @details Suppressed during hydration or without a provider; writes every spin value,
     * including rounded/clamped inactive fields, rather than only the sender. If an inherited
     * project model resolves to UEB and a UEB site value differs, sets an explicit UEB model
     * first. Legacy snow depths/rates/temperature retain inch-or-mm, hour and F-or-C
     * conventions; site heat/energy/depth fields retain W/m², kJ/m² and metres. No conversion or
     * local undo occurs.
     */
    void onFieldEdited_();
    /**
     * @brief Set provider model override from the combo and refresh field enablement.
     * @details Uses -1 for project default, zero degree-day or one UEB; hydration/unbound calls
     * do nothing. Retains inactive numerical values.
     */
    void onModelEdited_();
    /**
     * @brief Copy the trimmed removal-destination subcatchment ID into the provider.
     * @details Suppressed during hydration or without a provider; no name lookup/engine
     * validation occurs in this callback and no reference is retained to the widget text.
     */
    void onRemovalSubcatchEdited_();
    /**
     * @brief Synchronize visible name text after a registry rename of the current provider.
     * @param[in] p Borrowed renamed provider, ignored unless current.
     * @param[in] prev Previous ID, deliberately unused in the definition.
     * @param[in] now New ID copied into the name control if needed.
     * @details Temporarily suppresses field callbacks to avoid rename feedback.
     */
    void onProviderRenamed_(openswmmvis::snowpack::SnowpackProvider *p, const QString &prev,
                            const QString &now);

private:
    /**
     * @brief Create the owned list and grouped 32-parameter snowpack form once.
     * @details Builds 27 legacy plowable/impervious/pervious/removal fields plus five UEB site
     * fields from ordered specs, using labeled native units and per-field range/precision. Adds
     * a separate removal ID and model selector; Close uses accept(), including registry
     * validation. Requires uninitialized child-control pointers.
     */
    void buildUi_();
    /**
     * @brief Enable degree-day or UEB controls according to the current effective model.
     * @details UEB disables the first four degree-day fields in each surface group and enables
     * its five site fields; other shared initial/removal fields remain available. No provider
     * disables all numeric fields. Does not erase or convert retained values.
     */
    void updateModelFields_();
    /**
     * @brief Hydrate the guarded provider's fields and effective-model context.
     * @param[in] p Borrowed nullable registry-owned provider; null clears names and disables
     * editors.
     * @details When an engine model query succeeds, refreshes p's default-model metadata (which
     * can emit provider signals), then copies all values with this dialog's edit callbacks
     * suppressed. Display rounding/clamping does not itself rewrite numeric provider fields;
     * finally updates model-specific enablement.
     */
    void bindProvider_(openswmmvis::snowpack::SnowpackProvider *p);
    /**
     * @brief Select the provider's registry row or bind directly if no row is available.
     * @param[in] p Borrowed nullable provider; not owned, and direct fallback does not require
     * verified registry membership.
     * @details Normal row selection triggers binding through selectionChanged.
     */
    void selectProviderInList_(openswmmvis::snowpack::SnowpackProvider *p);
    /**
     * @brief Generate an unused SnowpackN identity without reserving it.
     * @return Owned name starting at providerCount()+1 and incrementing until unused; Snowpack1
     * without a registry.
     */
    QString suggestUniqueName_() const;

    QPointer<openswmmvis::snowpack::SnowpackRegistry> m_registry;
    QPointer<SWMMModelLayer> m_layer;
    QPointer<openswmmvis::snowpack::SnowpackProvider> m_current;
    Mode m_mode = Mode::Edit;

    QSplitter *m_splitter = nullptr;

    QListView *m_listView = nullptr;
    SnowpackListModel *m_listModel = nullptr;
    QPushButton *m_addBtn = nullptr;
    QPushButton *m_delBtn = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QComboBox *m_modelKind = nullptr;

    QVector<QDoubleSpinBox *> m_spins; ///< one per SnowpackProvider::Param
    QLineEdit *m_removalSubcatchEdit = nullptr;

    bool m_suppressFieldSync = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_SNOWPACKEDITORDIALOG_H
