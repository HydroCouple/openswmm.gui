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

#include "snowpack/snowpackprovider.h"

#include <QDialog>
#include <QHash>
#include <QPointer>
#include <QVector>

class QDoubleSpinBox;
class QLineEdit;
class QListView;
class QPushButton;
class QSplitter;
class QComboBox;
class QUndoStack;

class SWMMModelLayer;

namespace openswmmvis::snowpack
{
class SnowpackProvider;
class SnowpackRegistry;
}

namespace openswmmvis::ui
{

class SnowpackListModel;
class DataObjectDraftSession;
class DataObjectDraftFooter;
class DraftMarkProxyModel;

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
     * @details Binds the first provider. With a layer, completed field edits use the shared
     * checked writer and its canvas Undo stack; Close only dismisses accepted edits. Engine
     * close invalidates the editor. Without a layer, retains standalone provider drafting and
     * registry validation on acceptance. Rejection does not undo accepted changes.
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
     * otherwise empty. Project-owned fields validate immediately; standalone acceptance validates
     * and saves. Cancellation does not roll back
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
    /*! Undo stack for Apply, create, delete and rename (one step each). Without
     *  one the layer's map undo stack is used. */
    void setUndoStack(QUndoStack *stack);
    /*! Changes are held in a draft until Apply (plan
     *  workplans/DATA_OBJECT_APPLY_REVERT_PLAN_2026-11-11.md). */
    DataObjectDraftSession *draftSession() const noexcept { return m_session; }
    /*! Closing with unapplied changes asks Apply / Discard / Cancel first. */
    void done(int result) override;

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
     * @brief Create an unused SnowpackN pack at once (one undo step) and select it.
     * @details No-ops without registry.
     */
    void onAddClicked_();
    /**
     * @brief Confirm deletion (naming what uses the pack), delete it at once as one undo step
     * and select the first survivor.
     * @details Undo brings the pack back with its settings, but not the uses the engine
     * detached (plan D8). No-op on refusal; an empty registry leaves the form unbound.
     */
    void onDeleteClicked_();
    /**
     * @brief Trim/validate the current name and request registry rename.
     * @details Suppressed during hydration or without registry/provider. Empty/unchanged names
     * do nothing; malformed SWMM identifiers or failed rename produce a warning and restore
     * current provider text. A rename takes effect at once as one undo step.
     */
    void onNameEdited_();
    /**
     * @brief Draft the completed parameter; nothing reaches the model until Apply.
     * @details Hydration/unbound calls do nothing. A UEB site edit on an inheriting pack makes
     * the draft an explicit UEB pack. Depth/rate/temperature use input in-or-mm, hours and
     * F-or-C; UEB site heat, energy and extinction depth use W/m², kJ/m² and meters. No unit
     * conversion occurs.
     */
    void onFieldEdited_();
    /**
     * @brief Draft the model choice and refresh applicable fields.
     * @details Uses -1 for project default, zero degree-day or one UEB. Project default also
     * resets the UEB site values to their defaults in the draft. Hydration/unbound calls do
     * nothing.
     */
    void onModelEdited_();
    /**
     * @brief Draft the trimmed destination subcatchment.
     * @details Suppressed during hydration or without a provider.
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
     * a separate removal ID and model selector, and the Apply / Revert / Close footer.
     * Requires uninitialized child-control pointers.
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

    /// Make \p v the current pack's draft (dropped again when equal to the model). Each
    /// field handler changes only its own value, so display rounding never reaches the model.
    void setDraft_(const openswmmvis::snowpack::SnowpackProvider::Params &v);
    /// What the form shows for \p p: its draft if it has one, else the model's values.
    openswmmvis::snowpack::SnowpackProvider::Params
    shown_(openswmmvis::snowpack::SnowpackProvider *p) const;
    bool applyDrafts_(QString *reason);
    void revertDrafts_();
    QUndoStack *undoStack_() const;

    QHash<openswmmvis::snowpack::SnowpackProvider *,
          openswmmvis::snowpack::SnowpackProvider::Params>
        m_drafts;
    DataObjectDraftSession *m_session = nullptr;
    DataObjectDraftFooter *m_footer = nullptr;
    DraftMarkProxyModel *m_draftMarks = nullptr; ///< Marks rows holding unapplied changes.
    QPointer<QUndoStack> m_undo;
    bool m_applying = false;

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
