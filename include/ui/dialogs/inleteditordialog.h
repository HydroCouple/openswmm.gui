/**
 * @file      inleteditordialog.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Three-pane editor for SWMM inlet designs ([INLETS]).
 *
 * Modelled on TransectEditorDialog (Inlets plan §2.2). Layout (left → right,
 * a 1:3:3 QSplitter named "main" so the app-wide layout persistence picks it
 * up):
 *
 *   ┌────────────┬────────────────────────────┬────────────────────────┐
 *   │  Inlets    │  Name                      │  toolbar               │
 *   │  list view │  Description               │  (Fit / Zoom / Copy /  │
 *   │            │  Inlet Type (combo)        │   Export Image)        │
 *   │  [+ Add]   │  Property tree — the       │  InletDrawingView      │
 *   │  [- Delete]│  InletPropertyBag groups,  │  (plan + section with  │
 *   │            │  filtered by type          │   dimension callouts)  │
 *   └────────────┴────────────────────────────┴────────────────────────┘
 *
 * MVC contract: every mutation goes through InletProvider / InletRegistry,
 * wrapped in a QUndoCommand when the dialog has a QUndoStack (see
 * inletundocommands.h); the list, the tree and the drawing all re-render from
 * the provider's signals.
 *
 * Entry points mirror the transect editor: `createNew` (modeless, starts on a
 * fresh design), `openForInlet` (modeless, selects a design by name — used by
 * the Object Browser), and the modal `pickInlet` used by the inlet-usage
 * pickers, which preselects a compatible design and checks the final choice
 * against the host conduit's cross-section shape.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_INLETEDITORDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_INLETEDITORDIALOG_H

#include "inlet/inletprovider.h"

#include <QDialog>
#include <QPointer>

class QComboBox;
class QLabel;
class QLineEdit;
class QListView;
class QPropertyModel;
class QPushButton;
class QSplitter;
class QStatusBar;
class QTextEdit;
class QToolBar;
class QTreeView;
class QUndoStack;

class SWMMModelLayer;

namespace openswmmvis::inlet
{
class InletProvider;
class InletRegistry;
}

namespace openswmmvis::ui
{

class InletDrawingView;
class InletListModel;
class InletPropertyBag;

class InletEditorDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Mode { Edit, CreateNew };
    Q_ENUM(Mode)

    /**
     * @brief Build a nonmodal live inlet editor with optional undo-backed mutations.
     * @param[in] registry Borrowed nullable registry guarded through QPointer; providers remain
     * registry-owned.
     * @param[in] layer Borrowed optional model supplying engine/curve context, guarded through
     * QPointer.
     * @param[in] undoStack Borrowed optional undo owner guarded through QPointer; non-null
     * routes add/delete/name/type/comment/design edits through commands, null uses direct
     * provider/registry edits.
     * @param[in] parent Optional Qt owner of the floating dialog and child models/widgets.
     * @details GUI-thread editor subscribes provider state and invalidates when
     * registry/model/undo owner is destroyed or the model engine closes. Selects the first
     * registry entry when present; closing the editor does not roll back already-applied
     * changes.
     */
    InletEditorDialog(openswmmvis::inlet::InletRegistry *registry, SWMMModelLayer *layer,
                      QUndoStack *undoStack, QWidget *parent = nullptr);
    /**
     * @brief Destroy owned controls/property adapters/drawing without owning inlet providers.
     * @details Default destructor does not explicitly save, roll back or clear the external undo
     * stack.
     */
    ~InletEditorDialog() override;

    /**
     * @brief Allocate an unshown editor and immediately invoke undo-aware inlet creation.
     * @param[in] registry Borrowed nullable registry receiving creation; not owned.
     * @param[in] layer Borrowed optional model/curve context; not owned.
     * @param[in] undoStack Borrowed optional stack used by creation and later edits; not owned.
     * @param[in] parent Optional Qt owner of the allocated dialog.
     * @return Newly allocated dialog, even if creation fails; caller manages showing/deletion
     * and no delete-on-close attribute is added.
     * @details Sets CreateNew before invokeNew(); successful creation changes the mode back to
     * Edit. Creation is immediate rather than deferred until acceptance.
     */
    static InletEditorDialog *createNew(openswmmvis::inlet::InletRegistry *registry,
                                        SWMMModelLayer *layer, QUndoStack *undoStack,
                                        QWidget *parent = nullptr);

    /**
     * @brief Show/raise this modeless editor and select an available named design.
     * @param[in] name Borrowed identity; empty, missing or absent registry leaves current
     * selection unchanged.
     * @details Reuses this instance and does not allocate an inlet or change dialog ownership.
     *
     * @details Show (raising an already-open instance) and select \p name.
     */
    void openForInlet(const QString &name);

    /**
     * @brief Run a modal inlet chooser and return a host-compatible selected identity.
     * @param[in] registry Borrowed nullable registry; null returns empty immediately.
     * @param[in] layer Borrowed optional model/curve context; not owned.
     * @param[in] undoStack Borrowed optional undo owner; null uses direct live edits.
     * @param[in] parent Optional Qt owner of the stack-lifetime child dialog and final
     * compatibility warning.
     * @param[in] compatibleXsectShape Negative accepts any type; STREET accepts
     * grate/curb/combo/slotted, RECT_OPEN/TRAPEZOIDAL accept drop families, and CUSTOM works on
     * every shape.
     * @return Selected provider name only when a provider survives and passes compatibility,
     * otherwise empty; independent of the dialog's Accepted/Rejected result.
     * @details Preselects the first compatible registry provider, while the full registry list
     * remains available for editing. After exec(), flushes a surviving guarded registry without
     * checking save status, then validates the final selection and warns if incompatible. No
     * cancellation rollback is supplied.
     */
    static QString pickInlet(openswmmvis::inlet::InletRegistry *registry, SWMMModelLayer *layer,
                             QUndoStack *undoStack, QWidget *parent, int compatibleXsectShape = -1);

    /**
     * @brief Read the editor's current mode flag.
     * @return Edit normally; a successful onAddClicked_() resets it to Edit even after
     * createNew() initially set CreateNew.
     */
    Mode mode() const noexcept { return m_mode; }
    /**
     * @brief Borrow the guarded current inlet provider.
     * @return Registry-owned pointer or null when unbound/destroyed/invalidated; caller never
     * deletes it.
     */
    openswmmvis::inlet::InletProvider *currentProvider() const noexcept;

    // ── Test hooks ──────────────────────────────────────────────────────────
    /**
     * @brief Borrow the inlet list widget.
     * @return Dialog-owned QListView; valid until child/dialog destruction.
     */
    QListView *listView() const noexcept { return m_listView; }
    /**
     * @brief Borrow the registry-backed inlet list adapter.
     * @return Dialog-owned InletListModel; provider ownership remains with the registry.
     */
    InletListModel *listModel() const noexcept { return m_listModel; }
    /**
     * @brief Borrow the current design's name editor.
     * @return Dialog-owned QLineEdit; editingFinished uses the validated rename route.
     */
    QLineEdit *nameEdit() const noexcept { return m_nameEdit; }
    /**
     * @brief Borrow the legacy-order inlet-type selector.
     * @return Dialog-owned QComboBox whose user data stores InletType ordinals.
     */
    QComboBox *typeCombo() const noexcept { return m_typeCombo; }
    /**
     * @brief Borrow the type-sensitive inlet design property tree.
     * @return Dialog-owned QTreeView backed by the owned property bag/model; commits follow
     * commitDesign_().
     */
    QTreeView *propertyTree() const noexcept { return m_propertyTree; }
    /**
     * @brief Borrow the current inlet's plan/section illustration.
     * @return Dialog-owned InletDrawingView; no engine/provider ownership transfer.
     */
    InletDrawingView *drawingView() const noexcept { return m_drawing; }

    /**
     * @brief Trigger the same immediate creation route as Add.
     * @details No-ops without registry; a successfully found/created design is selected, mode
     * becomes Edit and the name is focused for replacement.
     */
    void invokeNew();
    /**
     * @brief Delete the current design without another confirmation prompt.
     * @details Requires guarded current provider/registry. Pushes DeleteInletCommand when an
     * undo stack survives, otherwise calls registry removal directly; provider-removal
     * subscriptions unbind the form. Does not inspect deletion success itself.
     */
    void deleteCurrentSilently();

public slots:
    /**
     * @brief Disconnect old mutation targets, unbind state and reject the disabled editor once.
     * @details Idempotently clears registry/layer/undo pointers before focus-out edits, binds
     * null to property bag/drawing/form, unbinds the list and prevents later selection handling.
     * Keeps already-applied edits and external undo history.
     *
     * @details End editing before the model, registry or undo owner is released.
     */
    void invalidateContext();

private slots:
    /**
     * @brief Bind the current list row while context and adapter controls remain valid.
     * @details Invalidated/missing-control calls do nothing; providerAt() determines null for an
     * invalid row. No registry mutation occurs.
     */
    void onListSelectionChanged_();
    /**
     * @brief Request a unique-name inlet through an undo command or direct registry creation.
     * @details No-ops without registry. Looks up the resulting provider, selects it, resets mode
     * to Edit and focuses/selects its name; failure to find a provider leaves the existing
     * binding. Creation is not staged for later acceptance.
     */
    void onAddClicked_();
    /**
     * @brief Confirm impact-aware design deletion before invoking deleteCurrentSilently().
     * @details Shows registry-reported usages that removal clears. Declined confirmation does
     * nothing; the deletion route rechecks current guarded targets after the nested message box
     * and uses available undo support.
     */
    void onDeleteClicked_();
    /**
     * @brief Trim/validate a name and avoid duplicate/no-op undo entries before rename.
     * @details Suppressed during hydration or without current registry/control. Empty/unchanged,
     * invalid SWMM identifiers and existing registry names restore the provider name with
     * blocked edit signals and show status when needed. Accepted changes push RenameInletCommand
     * or invoke registry rename directly.
     */
    void onNameEdited_();
    /**
     * @brief Commit changed plain-text comments through undo or directly to the provider.
     * @details Suppressed during hydration or without current/comments control; unchanged text
     * is a no-op. Does not trim comments or convert them to rich text.
     */
    void onCommentsEdited_();
    /**
     * @brief Commit a changed inlet type from combo user data.
     * @param[in] index Nonnegative combo index; negative, hydration, unbound or missing-combo
     * requests do nothing. Caller normally supplies an existing row.
     * @details Unchanged type is a no-op; pushes SetInletTypeCommand or calls setType()
     * directly. Does not apply the modal host-shape filter or validate every newly active
     * dimension here.
     */
    void onTypeComboChanged_(int index);
    /**
     * @brief Open the shared curve editor and validate/commit a returned custom-curve identity.
     * @details Requires current provider, model layer and curve registry. Passes the optional
     * undo stack into the modal picker, then rechecks dialog context/current layer before
     * applying the selected ID through commitDesign_(). Empty result retains the current curve;
     * curve creation/edits can occur independently of inlet assignment.
     */
    void onPickCurveClicked_();
    /**
     * @brief Render the current drawing scene at double resolution and copy it to the clipboard.
     * @details No-ops without drawing or a non-null rendered pixmap; successful clipboard
     * replacement reports status for three seconds. Does not write a file or change provider
     * values.
     */
    void onCopyClicked_();
    /**
     * @brief Ask for a PNG path and save a double-resolution drawing render.
     * @details Cancel leaves files unchanged. Empty/null rendering or save failure shows a
     * warning; success reports the destination for three seconds. Writes the chosen path using
     * QPixmap::save() and does not alter model/provider data.
     */
    void onExportImageClicked_();
    /**
     * @brief Synchronize the bound provider name and window title after registry rename.
     * @param[in] p Borrowed renamed provider; ignored unless current.
     * @param[in] prev Previous identity, deliberately unused in the definition.
     * @param[in] now New identity copied into the name control/title.
     * @details Blocks edit-control signals during synchronization to avoid rename feedback.
     */
    void onProviderRenamed_(openswmmvis::inlet::InletProvider *p, const QString &prev,
                            const QString &now);

private:
    /**
     * @brief Create the owned list, comments/type/property form, drawing, close row and status
     * once.
     * @details Installs a property-bag commit callback to commitDesign_(), live CRUD routes and
     * non-default Close button. Close calls QDialog::close(), preserving immediate edits.
     * Requires an uninitialized child-control tree.
     */
    void buildUi_();
    /**
     * @brief Add owned fit/zoom/copy/PNG-export drawing actions once.
     * @details No-ops without toolbar; actions operate on the drawing and GUI clipboard/file
     * dialogs, with fit/zoom shortcuts. Repeated calls would append duplicate actions rather
     * than rebuild.
     */
    void buildToolbar_();
    /**
     * @brief Create an owned QPropertyModel exposing the existing inlet property bag.
     * @details No-ops without tree/bag. Sets QObject metatype explicitly, uses the class row as
     * root when present, ensures a property delegate, expands rows and sizes headers. New models
     * are parented to the tree; replacing the view's model does not explicitly delete an earlier
     * model here.
     */
    void rebuildPropertyTree_();
    /**
     * @brief Show property rows used by the current inlet type and gate the curve chooser.
     * @details Finds translated display labels first and raw property names second, ignoring
     * unmatched rows. Enables Choose Curve only for a bound CUSTOM inlet; does not change hidden
     * provider values or validate host-shape compatibility.
     */
    void applyRowVisibility_();
    /**
     * @brief Reconnect and hydrate the guarded current inlet and its property/drawing adapters.
     * @param[in] p Borrowed nullable provider, forced null after context invalidation; remains
     * registry-owned.
     * @details Disconnects old provider-to-dialog subscriptions when pointer changes, listens
     * for parameter/comment updates, binds bag/drawing, copies name/comments with
     * synchronization suppressed and enables fields only while bound. Finishes with
     * syncFromProvider_(); no provider values are rewritten by hydration.
     */
    void bindProvider_(openswmmvis::inlet::InletProvider *p);
    /**
     * @brief Select an exact provider pointer only when it belongs to the current registry list.
     * @param[in] p Borrowed provider to locate; null, absent model/view or missing row is a
     * no-op.
     * @details Unlike direct binding, does not adopt an unrelated provider. Selection normally
     * triggers onListSelectionChanged_().
     */
    void selectProviderInList_(openswmmvis::inlet::InletProvider *p);
    /**
     * @brief Refresh type/property values, visibility, curve preview, hint and title from bound
     * state.
     * @details Blocks combo signals when selecting the provider's type, asks the property model
     * to refresh and clears the title/hint for no current provider. Does not mutate inlet design
     * or reset drawing extents explicitly.
     */
    void syncFromProvider_();
    /**
     * @brief Copy the bound CUSTOM design's known curve points into the drawing.
     * @details Requires a drawing; a live CUSTOM provider, nonempty ID and layer curve registry
     * supply the copied points. Missing context/non-CUSTOM/unknown curve produces an empty
     * curve. Does not validate the curve's kind or modify provider/registry data.
     */
    void refreshCustomCurve_();
    /**
     * @brief Set the permanent hint to the bound inlet type's localized label.
     * @details No-ops before hint creation and clears text when unbound; no engine/provider
     * changes.
     */
    void updateStatusBar_();
    /**
     * @brief Search Inlet1 through Inlet9998 for an unused registry identity.
     * @return First available owned candidate, translated Inlet1 without a registry or
     * translated Inlet after the bounded search is exhausted. The fallback is not guaranteed
     * unique and no name is reserved.
     */
    QString suggestUniqueName_() const;

    /**
     * @brief Validate a proposed design and publish through undo or the current provider.
     * @param[in] before Borrowed previous design snapshot expected to describe the current
     * provider; copied into an undo command when available, without an independent
     * equality/concurrency check here.
     * @param[in] after Borrowed desired native-unit design validated against active property
     * groups and available curve registry.
     * @details No-ops when unbound. Failed validation shows a five-second status and
     * rebinds/refreshes the property bag to stored values; success pushes SetInletParamsCommand
     * or sets the design directly. Validation permits partially specified CUSTOM curve pairs and
     * does not enforce the modal host-shape filter or universal finite-value checks.
     *
     * @details The InletPropertyBag's commit hook: validates, then pushes a
     *  SetInletParamsCommand (or writes the provider directly when the dialog
     *  has no undo stack). A rejected edit is reported in the status bar and
     *  the bag is re-synced from the provider.
     */
    void commitDesign_(const openswmmvis::inlet::InletDesignData &before,
                       const openswmmvis::inlet::InletDesignData &after);

    /**
     * @brief Test a non-null provider type against the modal host-shape rule.
     * @param[in] p Borrowed nullable provider; no ownership or mutation.
     * @return False for null; otherwise typeFitsShape() result, which accepts every type for a
     * negative filter and CUSTOM on every shape.
     */
    bool passesShapeFilter_(openswmmvis::inlet::InletProvider *p) const;

    bool m_contextValid = true;
    QPointer<openswmmvis::inlet::InletRegistry> m_registry;
    QPointer<SWMMModelLayer> m_layer;
    QPointer<QUndoStack> m_undoStack;
    QPointer<openswmmvis::inlet::InletProvider> m_current;
    Mode m_mode = Mode::Edit;
    int m_shapeFilter = -1;

    QSplitter *m_splitter = nullptr;

    // Left pane.
    QListView *m_listView = nullptr;
    InletListModel *m_listModel = nullptr;
    QPushButton *m_addBtn = nullptr;
    QPushButton *m_delBtn = nullptr;

    // Middle pane.
    QLineEdit *m_nameEdit = nullptr;
    QTextEdit *m_commentsEdit = nullptr;
    QComboBox *m_typeCombo = nullptr;
    QTreeView *m_propertyTree = nullptr;
    QPropertyModel *m_propertyModel = nullptr;
    InletPropertyBag *m_propertyBag = nullptr;
    QPushButton *m_pickCurveBtn = nullptr;

    // Right pane.
    QToolBar *m_toolBar = nullptr;
    InletDrawingView *m_drawing = nullptr;

    QStatusBar *m_status = nullptr;
    QLabel *m_hintLabel = nullptr;

    bool m_suppressSync = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_INLETEDITORDIALOG_H
