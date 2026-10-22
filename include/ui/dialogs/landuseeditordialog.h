/**
 * @file      landuseeditordialog.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Unified editor for SWMM land uses ([LANDUSES] + [BUILDUP] +
 *         [WASHOFF]) — iteration 4.
 *
 * List pane + tabbed detail pane for the selected land use:
 *   - General && Sweeping: name, sweep interval (days), removal fraction.
 *   - Buildup:  per-pollutant [BUILDUP] table (BuildupTableModel).
 *   - Washoff:  per-pollutant [WASHOFF] table incl. the per-pollutant
 *     sweep/BMP efficiencies (WashoffTableModel).
 * Rows in the two tables re-dimension automatically as pollutants are
 * added/removed (refreshPollutants(), wired to the pollutant registry).
 * Delete is impact-aware: the confirmation lists what cascades
 * (LandUseRegistry::impactSummary) before swmm_landuse_delete runs.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_LANDUSEEDITORDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_LANDUSEEDITORDIALOG_H

#include <QDialog>
#include <QPointer>

class QDoubleSpinBox;
class QLineEdit;
class QListView;
class QPushButton;
class QSplitter;
class QTableView;
class QTabWidget;

class SWMMModelLayer;

namespace openswmmvis::landuse
{
class LandUseProvider;
class LandUseRegistry;
}

namespace openswmmvis::pollutant
{
class PollutantRegistry;
}

namespace openswmmvis::ui
{

class BuildupTableModel;
class LandUseListModel;
class WashoffTableModel;

class LandUseEditorDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Mode { Edit, CreateNew };
    Q_ENUM(Mode)

    /**
     * @brief Build a provider-backed land-use list and sweeping/buildup/washoff editor.
     * @param[in] registry Borrowed nullable registry guarded by QPointer, supplying providers
     * and engine access; null leaves tables/form unbound.
     * @param[in] layer Borrowed optional model context guarded by QPointer; never owned.
     * @param[in] parent Optional Qt owner of the editor and child controls/models.
     * @details Selects the first registered provider when available. Uses GUI-thread live
     * provider/engine edits, with no dialog-local staging/rollback. Caller keeps the associated
     * engine valid through all editor operations and nested confirmations.
     */
    LandUseEditorDialog(openswmmvis::landuse::LandUseRegistry *registry, SWMMModelLayer *layer,
                        QWidget *parent = nullptr);
    /**
     * @brief Destroy Qt-owned controls/models without owning registry providers or engine.
     * @details Default destructor adds no explicit save, rollback or undo operation.
     */
    ~LandUseEditorDialog() override;

    /**
     * @brief Allocate an unshown CreateNew editor and immediately attempt a land-use creation.
     * @param[in] registry Borrowed nullable registry used to create/save a provider; not owned.
     * @param[in] layer Borrowed optional model context; not owned.
     * @param[in] parent Optional Qt owner of the allocated dialog.
     * @return Newly allocated editor; caller manages showing/deletion, and delete-on-close is
     * not enabled here. May remain unbound if creation fails or registry is null.
     * @details Creation is not contingent on later acceptance.
     */
    static LandUseEditorDialog *createNew(openswmmvis::landuse::LandUseRegistry *registry,
                                          SWMMModelLayer *layer, QWidget *parent = nullptr);

    /**
     * @brief Run a modal live editor and return its current land-use name on close.
     * @param[in] registry Borrowed registry required to remain live through exec() and the final
     * save; null returns empty immediately.
     * @param[in] layer Borrowed optional model context; not owned.
     * @param[in] initialName Existing identity to preselect; empty creates a new suggested-name
     * provider immediately. A missing nonempty identity leaves the initial selection.
     * @param[in] parent Optional Qt parent of the stack-lifetime modal dialog.
     * @return Current provider's copied ID or empty when none; independent of dialog result.
     * @details Always calls saveToEngine() after exec(), ignoring both results. Provider and
     * matrix edits are live; closing/rejecting does not roll them back.
     */
    static QString pickLandUse(openswmmvis::landuse::LandUseRegistry *registry,
                               SWMMModelLayer *layer, const QString &initialName,
                               QWidget *parent = nullptr);

    /**
     * @brief Report the entry-point mode flag.
     * @return Edit by default or CreateNew when explicitly set by a creation entry point; not
     * changed by selection.
     */
    Mode mode() const noexcept { return m_mode; }
    /**
     * @brief Borrow the guarded current land-use provider.
     * @return Registry-owned pointer or null when unbound/destroyed; no ownership transfer.
     */
    openswmmvis::landuse::LandUseProvider *currentProvider() const noexcept;

    /**
     * @brief Borrow the initialized land-use list widget.
     * @return Dialog-owned QListView, valid until its Qt owner destroys it.
     */
    QListView *listView() const noexcept { return m_listView; }
    /**
     * @brief Borrow the registry-to-list adapter.
     * @return Dialog-owned LandUseListModel; providers remain registry-owned.
     */
    LandUseListModel *listModel() const noexcept { return m_listModel; }
    /**
     * @brief Borrow the current-provider name control.
     * @return Dialog-owned QLineEdit; normal editingFinished invokes validated rename.
     */
    QLineEdit *nameEdit() const noexcept { return m_nameEdit; }
    /**
     * @brief Borrow the currently bound per-pollutant buildup table model.
     * @return Dialog-owned BuildupTableModel, editing the bound engine land-use matrix directly.
     */
    BuildupTableModel *buildupModel() const noexcept { return m_buildupModel; }
    /**
     * @brief Borrow the currently bound per-pollutant washoff table model.
     * @return Dialog-owned WashoffTableModel, including per-pollutant sweep/BMP efficiencies.
     */
    WashoffTableModel *washoffModel() const noexcept { return m_washoffModel; }

    /**
     * @brief Invoke the immediate New-button provider creation/save route.
     * @details Does not show the window or alter the entry-point mode; null registry is a no-op.
     */
    void invokeNew();

    /**
     * @brief Show/raise this modeless editor and select a matching provider if found.
     * @param[in] name Borrowed land-use ID; empty or unknown name preserves the existing
     * selection.
     * @details Does not create a provider or transfer dialog ownership.
     *
     * @details Modeless open-for-edit: shows the dialog and selects \p name.
     *  Used by the Object Browser, Properties panel and Attribute Table.
     */
    void openForLandUse(const QString &name);

    /**
     * @brief Subscribe the quality tables to another registry's pollutant membership/name
     * changes.
     * @param[in] registry Borrowed optional pollutant registry whose QObject signals drive
     * refreshPollutants(); null is a no-op and no ownership is acquired.
     * @details Connects added, about-to-remove and renamed notifications. Does not disconnect
     * earlier subscriptions or deduplicate repeated calls; Qt disconnects when sender or this
     * dialog dies.
     *
     * @details Follow \a registry so the Buildup/Washoff rows re-dimension live as
     *  pollutants are added/removed/renamed (wired by the launch site).
     */
    void trackPollutantRegistry(openswmmvis::pollutant::PollutantRegistry *registry);

public slots:
    /**
     * @brief Ask existing buildup/washoff models to refresh their pollutant dimension.
     * @details Safe before either model exists; performs no explicit provider creation or
     * table-cell edit in this method.
     *
     * @details Re-dimension the Buildup/Washoff rows after the pollutant set
     *  changed (add/remove/rename).
     */
    void refreshPollutants();

private slots:
    /**
     * @brief Rebind details to the currently selected provider or null for invalid selection.
     * @details Reads list-model row mapping without creating or deleting registry entries.
     */
    void onListSelectionChanged_();
    /**
     * @brief Create a unique LandUseN provider, save registry state to the engine and select it.
     * @details No-ops without a registry or failed creation. Calls saveToEngine() immediately so
     * direct matrix models can resolve the new engine index; does not inspect save status or
     * create undo.
     */
    void onAddClicked_();
    /**
     * @brief Confirm an impact summary and remove the current land use through the registry.
     * @details Describes cascading buildup/washoff and coverage effects, then selects the first
     * survivor or unbinds. Uses a nested message box; caller must keep the registry/model
     * context stable through confirmation. No local rollback/undo or result status is supplied.
     */
    void onDeleteClicked_();
    /**
     * @brief Rename the current provider after trimming and SWMM-identifier validation.
     * @details Suppressed during hydration or without current registry/provider. Empty/unchanged
     * input does nothing; invalid syntax or failed rename shows a warning and restores the
     * provider's current name. Registry rename handles engine identity propagation.
     */
    void onNameEdited_();
    /**
     * @brief Copy both sweeping controls to the bound provider on either numeric edit.
     * @details Suppressed during hydration or without a provider. Writes sweep interval in days
     * and removal as a zero-to-one fraction using the current rounded/clamped widget values; it
     * does not isolate the sender field, convert units or stage an undo entry.
     */
    void onFieldEdited_();
    /**
     * @brief Synchronize visible text after the current provider's registry rename.
     * @param[in] p Borrowed renamed provider, acted on only when current.
     * @param[in] prev Previous ID, intentionally unused by the implementation.
     * @param[in] now New ID copied to the name widget if needed.
     * @details Temporarily suppresses field synchronization to avoid feedback mutations.
     */
    void onProviderRenamed_(openswmmvis::landuse::LandUseProvider *p, const QString &prev,
                            const QString &now);

private:
    /**
     * @brief Create owned list, sweeping controls and direct matrix-table views once.
     * @details Sweep interval uses days with nonnegative range; removal uses a zero-to-one
     * fraction. Close actions accept the live editor rather than rolling back. Requires
     * uninitialized control pointers.
     */
    void buildUi_();
    /**
     * @brief Hydrate scalar details and bind both quality models to one engine land-use index.
     * @param[in] p Borrowed nullable provider stored via QPointer; null clears/disables scalar
     * fields and binds matrix models to an invalid index.
     * @details Suppresses scalar mutation callbacks while copying display values, then resolves
     * p's name in the registry's borrowed engine. Failed/missing lookup leaves direct matrix
     * models unbound; no provider or engine data is changed here.
     */
    void bindProvider_(openswmmvis::landuse::LandUseProvider *p);
    /**
     * @brief Select a matching registry row, falling back to direct form binding.
     * @param[in] p Borrowed nullable provider; no ownership transfer or mandatory membership
     * check on the fallback path.
     * @details Valid row selection drives binding through selectionChanged; null or absent row
     * invokes bindProvider_() immediately.
     */
    void selectProviderInList_(openswmmvis::landuse::LandUseProvider *p);
    /**
     * @brief Find an unused LandUseN identity without creating it.
     * @return Owned candidate beginning at providerCount()+1 and incremented until unused, or
     * LandUse1 without a registry; does not reserve the name.
     */
    QString suggestUniqueName_() const;

    QPointer<openswmmvis::landuse::LandUseRegistry> m_registry;
    QPointer<SWMMModelLayer> m_layer;
    QPointer<openswmmvis::landuse::LandUseProvider> m_current;
    Mode m_mode = Mode::Edit;

    QSplitter *m_splitter = nullptr;

    QListView *m_listView = nullptr;
    LandUseListModel *m_listModel = nullptr;
    QPushButton *m_addBtn = nullptr;
    QPushButton *m_delBtn = nullptr;

    QLineEdit *m_nameEdit = nullptr;
    QDoubleSpinBox *m_intervalSpin = nullptr;
    QDoubleSpinBox *m_removalSpin = nullptr;

    QTabWidget *m_tabs = nullptr;
    QTableView *m_buildupView = nullptr;
    QTableView *m_washoffView = nullptr;
    BuildupTableModel *m_buildupModel = nullptr;
    WashoffTableModel *m_washoffModel = nullptr;

    bool m_suppressFieldSync = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_LANDUSEEDITORDIALOG_H
