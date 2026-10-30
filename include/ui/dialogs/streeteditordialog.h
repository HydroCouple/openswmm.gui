/**
 * @file      streeteditordialog.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Three-pane CRUD editor for SWMM street cross-sections ([STREETS]).
 *
 * Mirrors TransectEditorDialog's role and entry points (createNew /
 * pickStreet) but a street is parametric, so the middle pane is a field
 * form (nine scalar TStreet parameters + sides) rather than a station table,
 * and the right pane is a schematic section preview rather than a point
 * chart.
 *
 * Layout (left → right):
 *
 *   ┌──────────────┬───────────────────────────┬────────────────────────┐
 *   │  Streets     │  Name (QLineEdit)         │  Section preview       │
 *   │  list view   │  Road Width / Curb Height │  (gutter + crown +     │
 *   │              │  Cross Slope / Roughness  │   backing schematic)   │
 *   │  [+ New]     │  Gutter Depression / Width│                        │
 *   │  [- Delete]  │  Sides (1 / 2)            │                        │
 *   │              │  Backing W / Slope / n    │                        │
 *   └──────────────┴───────────────────────────┴────────────────────────┘
 *
 * MVC contract — every mutation goes through StreetProvider; subscribed
 * registry signals keep the list, field form, and preview in lock-step.
 *
 * Legacy parity: EPA SWMM-GUI Dstreet.pas (TStreetEditorForm) + the
 * combo/edit-button launch path in Dxsect.pas (TsectBtnClick).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_STREETEDITORDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_STREETEDITORDIALOG_H

#include <QDialog>
#include <QPointer>
#include <QWidget>

class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QListView;
class QPushButton;
class QRadioButton;
class QSplitter;

class SWMMModelLayer;

namespace openswmmvis::street
{
class StreetProvider;
class StreetRegistry;
}

namespace openswmmvis::ui
{

class StreetListModel;

/*! \brief Lightweight schematic preview of a street cross-section. Draws
 *  the road crown, curb, depressed gutter and (optionally) backing from a
 *  StreetProvider's parameters. No Q_OBJECT — plain paintEvent widget. */
class StreetSectionPreview : public QWidget
{
public:
    /**
     * @brief Create a schematic street-section widget with a 220-by-160 minimum size.
     * @param[in] parent Optional Qt owner; the widget enables automatic background fill and owns
     * no street provider.
     */
    explicit StreetSectionPreview(QWidget *parent = nullptr);
    /**
     * @brief Change the guarded street whose parameters drive the preview.
     * @param[in] p Borrowed nullable provider, stored through QPointer; null displays the
     * no-selection placeholder.
     * @details Same-pointer calls do nothing. Disconnects previous provider-to-preview signals,
     * subscribes the new provider's paramsChanged to QWidget::update and schedules repaint; no
     * provider mutation or ownership transfer.
     */
    void setProvider(openswmmvis::street::StreetProvider *p);

protected:
    /**
     * @brief Paint the provider's parametric street section with independent horizontal/vertical
     * scaling.
     * @param[in] event Borrowed paint notification, intentionally unused; paints the complete
     * widget rectangle.
     * @details Reads native-length geometry and percentage slopes, clamps negative geometry to
     * zero for the schematic and mirrors a second side only for sides()==2. Draws curb, gutter,
     * crown and backing topology with vertical exaggeration, fill and a zero-elevation
     * reference. No provider draws a placeholder; fallback extents avoid zero scale
     * denominators. Does not mutate provider values or promise a hydraulic result.
     */
    void paintEvent(QPaintEvent *event) override;

private:
    QPointer<openswmmvis::street::StreetProvider> m_provider;
};

class StreetEditorDialog : public QDialog
{
    Q_OBJECT
public:
    enum class Mode { Edit, CreateNew };
    Q_ENUM(Mode)

    /**
     * @brief Build a live street-provider editor and parametric section preview.
     * @param[in] registry Borrowed nullable registry stored through QPointer; providers remain
     * registry-owned.
     * @param[in] layer Borrowed optional model context stored through QPointer; engine
     * closure/destruction invalidates the editor.
     * @param[in] parent Optional Qt owner of the dialog and children.
     * @details GUI-thread editor selects the first available provider, subscribes registry
     * events and commits edits immediately to providers. Closing does not undo changes. Registry
     * destruction also invalidates the context before later focus-out edits can use it.
     */
    StreetEditorDialog(openswmmvis::street::StreetRegistry *registry, SWMMModelLayer *layer,
                       QWidget *parent = nullptr);
    /**
     * @brief Destroy owned controls/preview without deleting registry-owned streets.
     * @details Default destructor does not explicitly flush or roll back registry values.
     */
    ~StreetEditorDialog() override;

    /**
     * @brief Allocate an unshown CreateNew editor and immediately request a new street.
     * @param[in] registry Borrowed nullable registry used for creation; not owned.
     * @param[in] layer Borrowed optional model/engine context; not owned.
     * @param[in] parent Optional Qt parent owning the returned dialog.
     * @return Newly allocated editor, even if no provider could be created; caller manages
     * showing/deletion and no delete-on-close attribute is set.
     * @details Creation is not deferred until dialog acceptance.
     */
    static StreetEditorDialog *createNew(openswmmvis::street::StreetRegistry *registry,
                                         SWMMModelLayer *layer, QWidget *parent = nullptr);

    /**
     * @brief Run a modal live editor and flush a still-valid registry on closure.
     * @param[in] registry Borrowed nullable registry; null returns empty without a dialog.
     * @param[in] layer Borrowed optional model context; not owned.
     * @param[in] initialName Borrowed street identity to preselect when found; empty creates a
     * new suggested street immediately. Missing nonempty names leave the constructor's initial
     * selection.
     * @param[in] parent Optional Qt owner of the stack-lifetime modal dialog.
     * @return Current provider name or empty when unbound/invalidated, independent of
     * Accepted/Rejected result.
     * @details Ignores exec() result. After closing, saveToEngine() is called only through the
     * surviving guarded registry; its status is ignored. Earlier live changes are not rolled
     * back.
     *
     * @details Modal pick / create / edit entry point — mirrors
     *  TransectEditorDialog::pickTransect. Empty \p initialName →
     *  CreateNew mode; otherwise → Edit mode pre-selecting that street.
     *  Returns the name of the street bound to the dialog on close, or
     *  empty if none. After close the registry is flushed to the engine.
     */
    static QString pickStreet(openswmmvis::street::StreetRegistry *registry, SWMMModelLayer *layer,
                              const QString &initialName, QWidget *parent = nullptr);

    /**
     * @brief Return the entry-point mode flag.
     * @return Edit by default or CreateNew when set by a creation entry point, independent of
     * the selected street.
     */
    Mode mode() const noexcept { return m_mode; }
    /**
     * @brief Borrow the guarded current street provider.
     * @return Registry-owned pointer or null after unbinding, provider destruction or context
     * invalidation; no ownership transfer.
     */
    openswmmvis::street::StreetProvider *currentProvider() const noexcept;

    // ── Test hooks ──────────────────────────────────────────────────────────
    /**
     * @brief Borrow the street list widget.
     * @return Dialog-owned initialized QListView; valid until its Qt owner destroys it.
     */
    QListView *listView() const noexcept { return m_listView; }
    /**
     * @brief Borrow the registry-backed street list adapter.
     * @return Dialog-owned StreetListModel; underlying providers remain registry-owned.
     */
    StreetListModel *listModel() const noexcept { return m_listModel; }
    /**
     * @brief Borrow the current street's name control.
     * @return Dialog-owned QLineEdit; editingFinished follows the validated interactive rename
     * route.
     */
    QLineEdit *nameEdit() const noexcept { return m_nameEdit; }

    /**
     * @brief Invoke immediate New-button street creation.
     * @details No-ops without registry; does not show the dialog or alter mode().
     */
    void invokeNew();

    /**
     * @brief Show/raise this modeless editor and select an available named street.
     * @param[in] name Borrowed identity; empty/missing identity or registry leaves selection
     * unchanged.
     * @details Does not create providers or change dialog ownership.
     *
     * @details Modeless open-for-edit: shows the dialog and selects \p name.
     *  Used by the Object Browser, Properties panel and Attribute Table.
     */
    void openForStreet(const QString &name);
    /**
     * @brief Remove the current provider through the registry without a confirmation dialog.
     * @details No-ops without registry/current provider. Clears the current pointer before
     * removal, then selects the first survivor or unbinds. Does not inspect removal status or
     * add dialog-local undo.
     */
    void deleteCurrentSilently();
    /**
     * @brief Request registry rename directly without interactive trimming/identifier warnings.
     * @param[in] newName Borrowed proposed identity passed unchanged to registry->rename().
     * @return False when registry/current provider is absent, otherwise the registry's success
     * result.
     * @details Registry enforces its own validation/identity propagation; this entry point does
     * not duplicate onNameEdited_()'s UI checks.
     */
    bool renameCurrent(const QString &newName);

public slots:
    /**
     * @brief Disconnect old mutation context, unbind controls and reject the disabled editor
     * once.
     * @details Idempotently clears guarded registry/layer, disables the preview/form through
     * null binding, unbinds the list and prevents later selection callbacks. Keeps
     * already-applied provider/engine changes.
     *
     * @details End editing before the model, registry or undo owner is released.
     */
    void invalidateContext();

private slots:
    /**
     * @brief Bind the selected street row while the context is valid.
     * @details Invalidated calls do nothing; an invalid current index binds null without
     * mutating registry data.
     */
    void onListSelectionChanged_();
    /**
     * @brief Ask the registry for a unique StreetN provider and select it if created.
     * @details No-ops without registry; registry owns the new provider and its materialization
     * behavior.
     */
    void onAddStreetClicked_();
    /**
     * @brief Ask for deletion confirmation, then invoke the current-provider removal route.
     * @details No-ops without registry/current provider or when declined. After the nested
     * message box, deleteCurrentSilently() rechecks current guarded targets; no transaction or
     * local undo is added.
     */
    void onDeleteStreetClicked_();
    /**
     * @brief Trim and validate a SWMM identifier before interactive registry rename.
     * @details Suppressed during hydration or without live targets. Empty/unchanged names do
     * nothing; invalid syntax or failed rename shows a warning and restores provider text if a
     * current provider still exists after the nested message box.
     */
    void onNameEdited_();
    /**
     * @brief Copy all nine displayed scalar street values to the current provider.
     * @details Suppressed during hydration or when unbound. Uses every widget's current
     * rounded/clamped value on any numeric edit: native model lengths for
     * widths/heights/depression, percent for cross/backing slopes and Manning n for roughness.
     * Does not isolate the sender, convert units, change sides or create undo.
     */
    void onFieldEdited_();
    /**
     * @brief Commit one versus two sides from the radio-button state.
     * @details Suppressed during hydration/unbound state; writes one when the one-sided button
     * is checked, otherwise two, without changing other scalar controls.
     */
    void onSidesToggled_();
    /**
     * @brief Receive the registry addition notification without extra work.
     * @param[in] p Newly added borrowed provider, intentionally unused; the list model maintains
     * its own subscription.
     * @details Does not select or bind the new provider itself.
     */
    void onProviderAdded_(openswmmvis::street::StreetProvider *p);
    /**
     * @brief Refresh visible text after the currently bound street is renamed.
     * @param[in] p Borrowed renamed provider, ignored unless current.
     * @param[in] prev Previous identity, deliberately unused by the definition.
     * @param[in] now New identity copied into the name editor when needed.
     * @details Temporarily suppresses field callbacks; does not perform another registry rename.
     */
    void onProviderRenamed_(openswmmvis::street::StreetProvider *p, const QString &prev,
                            const QString &now);

private:
    /**
     * @brief Create the owned list, nine-scalar/two-side form and preview once.
     * @details Configures nonnegative four-decimal fields, percentage slope labels and Manning
     * roughness, then wires direct provider edits and Close-to-accept actions. Requires
     * uninitialized control pointers; no street is created here.
     */
    void buildUi_();
    /**
     * @brief Hydrate the detail form and preview for one guarded street provider.
     * @param[in] p Borrowed nullable provider, forcibly treated as null if context was
     * invalidated.
     * @details Updates preview subscription, suppresses edit callbacks during widget hydration,
     * enables fields only for a provider and selects the side radio from its value. Null clears
     * the name while leaving disabled numerical displays; hydration does not rewrite provider
     * values.
     */
    void bindProvider_(openswmmvis::street::StreetProvider *p);
    /**
     * @brief Select the provider's registry row or bind the supplied provider directly.
     * @param[in] p Borrowed nullable provider; not owned and direct fallback does not
     * independently require registry membership.
     * @details Normal list selection triggers binding through selectionChanged; null/absent-row
     * cases bind immediately.
     */
    void selectProviderInList_(openswmmvis::street::StreetProvider *p);
    /**
     * @brief Find an unused StreetN identity without creating/reserving it.
     * @return Owned candidate beginning at providerCount()+1 and incrementing until registry
     * hasName() is false; Street1 without a registry.
     */
    QString suggestUniqueName_() const;

    bool m_contextValid = true;
    QPointer<openswmmvis::street::StreetRegistry> m_registry;
    QPointer<SWMMModelLayer> m_layer;
    QPointer<openswmmvis::street::StreetProvider> m_current;
    Mode m_mode = Mode::Edit;

    QSplitter *m_splitter = nullptr;

    // Left pane.
    QListView *m_listView = nullptr;
    StreetListModel *m_listModel = nullptr;
    QPushButton *m_addBtn = nullptr;
    QPushButton *m_delBtn = nullptr;

    // Middle pane (field form).
    QLineEdit *m_nameEdit = nullptr;
    QDoubleSpinBox *m_crownWidthSpin = nullptr;
    QDoubleSpinBox *m_curbHeightSpin = nullptr;
    QDoubleSpinBox *m_crossSlopeSpin = nullptr;
    QDoubleSpinBox *m_roadRoughSpin = nullptr;
    QDoubleSpinBox *m_gutterDepSpin = nullptr;
    QDoubleSpinBox *m_gutterWidthSpin = nullptr;
    QRadioButton *m_oneSidedRadio = nullptr;
    QRadioButton *m_twoSidedRadio = nullptr;
    QDoubleSpinBox *m_backWidthSpin = nullptr;
    QDoubleSpinBox *m_backSlopeSpin = nullptr;
    QDoubleSpinBox *m_backRoughSpin = nullptr;

    // Right pane.
    StreetSectionPreview *m_preview = nullptr;

    bool m_suppressFieldSync = false;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_STREETEDITORDIALOG_H
