/**
 * @file      hydrographgroupeditor.h
 * @brief     Slice BS Phase 6.9.2 — Unit-hydrograph editor dialog.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Slice BS Phase 6.9.2 — Unit-hydrograph editor dialog.
 *
 * Three-pane non-modal editor for [HYDROGRAPHS] / [RDII_DECAY] data:
 *   - Pane 1: groups list (QListView bound to HydrographGroupListModel)
 *             with a toolbar of New / Delete / Rename actions and a
 *             search field over a QSortFilterProxyModel.
 *   - Pane 2: name label + rain-gage combo + season combo + tab widget
 *             with three table views: RTK, linear initial abstraction and
 *             exponential decay, bound to HydrographRtkTableModel,
 *             HydrographIaTableModel and HydrographDecayTableModel.
 *   - Pane 3: live UH preview plot (three filled triangles + a composite
 *             summation series). Uses InteractiveChartView for standard
 *             rubber-band zoom + wheel zoom + pan + reset-to-extent.
 *
 * The dialog is pure view code — it owns no engine state. All mutations
 * route through SWMMModelLayer::applyHydrograph* / applyRdiiDecay*
 * helpers and rely on the layer's hydrographChanged(uhName) signal to
 * keep every subscribed view (this editor, Object Browser,
 * SWMMHydrographPropertyAdapter, NodeCompoundEditDialog's UH picker)
 * synchronized.
 *
 * Non-modal — matches the ProfileOptionsDialog convention so the user
 * can keep the editor open while clicking around the network for
 * calibration context. Geometry + splitter sizes persist via QSettings.
 *
 * See: docs/GUI_IMPLEMENTATION_PLAN.md Slice BS Phase 6.9.2.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef HYDROGRAPHGROUPEDITOR_H
#define HYDROGRAPHGROUPEDITOR_H

#include <QDialog>
#include <QPointer>
#include <QString>

class QListView;
class QTableView;
class QTabWidget;
class QComboBox;
class QLabel;
class QLineEdit;
class QSplitter;
class QSortFilterProxyModel;
class QToolButton;
class QDialogButtonBox;
class QGraphicsSimpleTextItem;

class SWMMModelLayer;
class HydrographGroupListModel;
class HydrographRtkTableModel;
class HydrographIaTableModel;
class HydrographDecayTableModel;

QT_BEGIN_NAMESPACE
namespace QtCharts
{
} // forward
QT_END_NAMESPACE

class QChart;
class QAreaSeries;
class QLineSeries;
class QValueAxis;
class QLegendMarker;
namespace openswmmvis::ui
{
class InteractiveChartView;
class ChartAxisFormatController;
}

class HydrographGroupEditor : public QDialog
{
    Q_OBJECT
public:
    /**
     * @brief Build a non-modal editor over borrowed layer-owned hydrograph models.
     * @param[in] layer Nullable model layer, guarded by QPointer; models/engine remain
     * layer-owned. Destruction or engineAboutToClose invalidates the editor.
     * @param[in] parent Optional Qt parent owning the dialog.
     * @details GUI-thread use only. Owns proxy, controls and chart; selects the first visible
     * group when available. Table/registry changes are applied live through layer helpers;
     * closing does not roll them back. Table contexts are shared layer models, so this editor
     * does not own independent draft tables.
     */
    explicit HydrographGroupEditor(SWMMModelLayer *layer, QWidget *parent = nullptr);
    /**
     * @brief Release dialog-owned controls/proxy/chart through Qt ownership.
     * @details Layer-owned hydrograph models and already-applied edits survive this dialog; no
     * transaction rollback is performed.
     */
    ~HydrographGroupEditor() override;

    /**
     * @brief Show/raise a valid editor and select a visible matching group.
     * @param[in] name Borrowed exact group identity; empty, unknown or currently filtered-out
     * names leave selection unchanged.
     * @details Invalidated context is a no-op. A missing name still raises the dialog; does not
     * create groups or clear the active filter.
     *
     * @details Select \a name in the groups list and make sure the dialog is
     * visible / raised. Unknown/filtered names leave the selection unchanged.
     */
    void openForGroup(const QString &name);

public slots:
    /**
     * @brief Permanently detach borrowed models and reject the editor before engine release.
     * @details Idempotent; disconnects layer signals, clears model pointers before detaching
     * views to guard emitted selection changes, disables controls and rejects. Does not delete
     * layer models or undo applied edits.
     *
     * @details Detach layer-owned models before the layer or engine is released.
     */
    void invalidateContext();

    /**
     * @brief Raise the valid editor and run the normal New-name prompt.
     * @details Invalid context is ignored; creation/validation/selection are delegated to
     * onNewGroup().
     *
     * @details Slice BM.0-Add-New — bring the dialog forward and trigger the
     *  same name-prompt + create flow the left-pane "New" button drives.
     *  External Add-New entrypoints (Object Browser Data section, future
     *  Data menu) call this instead of poking m_addBtn directly.
     */
    void beginNewGroup();

    /**
     * @brief Run a modal live-editing group picker and return its final highlighted identity.
     * @param[in] layer Borrowed nullable layer providing shared models; no engine/model
     * ownership transfer.
     * @param[in] initialName Exact preferred group; absent/unknown values retain the
     * constructor's first selection.
     * @param[in] parent Optional Qt parent for the stack-owned modal dialog.
     * @return Current highlighted group name after exec(), regardless of accepted/rejected
     * dialog result; empty when no valid selection remains. Dismissing does not itself force an
     * empty result or roll back live edits.
     * @details Permits creation and editing while picking. No dedicated accept-only transaction
     * is introduced.
     *
     * @details Run the editor as a modal "pick a unit hydrograph" dialog. Opens
     *  pre-selected on \a initialName (or the first group if empty), lets
     *  the user create / edit / pick a group with full MVC sync, then
     *  returns the final highlighted name regardless of the dialog result.
     * Edits are live; dismissal does not roll them back.
     */
    static QString pickGroup(SWMMModelLayer *layer, const QString &initialName,
                             QWidget *parent = nullptr);

protected:
    /**
     * @brief Forward the visibility event to QDialog without extra refresh work.
     * @param[in,out] e Borrowed non-null Qt show event; not retained.
     */
    void showEvent(QShowEvent *e) override;

private slots:
    /**
     * @brief Rebind shared table contexts and refresh controls for the highlighted group.
     * @details Invalidated context is ignored. Reads rain-gage assignments using 64-byte
     * identity buffers, skips failed rows, defaults to no gage when unmatched, and suppresses
     * recursive gage writes during hydration. Refreshes preview/summary and disables
     * group-specific actions for an empty selection.
     */
    void onGroupSelectionChanged();
    /**
     * @brief Flush RTK/IA cell selection and rebind to the combo's current season.
     * @param[in] index Ignored signal index; currentMonth() reads the combo's stored month data.
     * @details Suppressed during season hydration or invalidated context. Clears the two
     * season-dependent views' current indexes before rebinding; decay context remains
     * group-only. Refreshes the plot without writing a season value itself.
     */
    void onSeasonChanged(int index);
    /**
     * @brief Publish the selected group's current combo gage through the layer helper.
     * @param[in] index Ignored signal index; item data supplies the gage identity, including
     * empty for none.
     * @details Suppressed during hydration; missing group/layer is ignored. Does not inspect
     * helper success or perform local rollback/undo independently.
     */
    void onGageChanged(int index);
    /**
     * @brief Prompt for a trimmed unique name and create a live group with no assigned gage.
     * @details Rechecks layer/context after the modal prompt, creates initial response zero
     * through applyHydrographAddGroup(), warns on refusal and selects the resulting group on
     * success. Cancel/empty input is a no-op; does not directly write engine tables.
     */
    void onNewGroup();
    /**
     * @brief Confirm deletion of the selected group and its related RDII assignments.
     * @details Rechecks context after the modal question; calls the layer's removal helper and
     * warns on refusal. The prompt covers seasonal/response rows, gage, decay rows and
     * referencing node assignments; no local restoration is attempted.
     */
    void onDeleteGroup();
    /**
     * @brief Prompt for a replacement identity and rename via the layer helper.
     * @details Cancel, empty/unchanged text or invalidated context is ignored. Warns on refusal
     * and selects the new visible name after success; shared model notifications handle data
     * refresh.
     */
    void onRenameGroup();
    /**
     * @brief Refresh plot/gage chrome for a changed current group and always refresh summary.
     * @param[in] uhName Exact changed identity, or empty to refresh the current group's
     * gage/preview unconditionally.
     * @details Other named groups update summary only. Skips failed engine gage reads and
     * suppresses setter callbacks while hydrating; shared models handle their own resets.
     * Invalidated context is ignored.
     */
    void onHydrographChanged(const QString &uhName);
    /**
     * @brief Update the case-insensitive fixed-string group filter.
     * @param[in] text Borrowed literal search text, copied by the proxy; empty shows all rows.
     * @details Does not interpret a regular expression or mutate group identities.
     */
    void onFilterTextChanged(const QString &text);
    /**
     * @brief Flush focused table editors and refresh the plot while keeping the editor open.
     * @details Invalidated context is ignored. Edits already publish through model setData; no
     * separate atomic Apply transaction or rollback baseline is created.
     */
    void onApplyClicked();
    /**
     * @brief Flush table editors, refresh the plot and close a valid editor.
     * @details Uses close() rather than accept(); live edits remain regardless of dialog result.
     * Invalid context is a no-op.
     */
    void onOkClicked();
    /**
     * @brief Toggle the sender marker's series and dim its label when hidden.
     * @details Requires a QLegendMarker sender with a series; otherwise ignored. Keeps the
     * marker visible for toggling back; a later preview refresh restores response visibility
     * from available data.
     */
    void onLegendMarkerClicked();
    /**
     * @brief Show or hide a viewport-clamped tooltip for a plotted point.
     * @param[in] point Borrowed chart data coordinate: x in hours and y in unit-flow ordinates;
     * not screen pixels.
     * @param[in] state True displays the sender series name/time/value, false hides the label.
     * @details No-op without chart/view/label. Maps chart values to view coordinates, clamps
     * placement and changes presentation only.
     */
    void onPlotHover(const QPointF &point, bool state);
    /**
     * @brief Open local series/legend/axis formatting and zoom actions.
     * @param[in] globalPos Borrowed screen-pixel position for the blocking QMenu::exec().
     * @details No-op without chart. Dialog-owned actions adjust response fill/outline, composite
     * style, visibility, antialiasing and axis format; canceled color choices do nothing.
     * Refreshing extent recomputes response visibility. Does not change hydraulic parameters or
     * persist model edits.
     */
    void showPlotStyleMenu(const QPoint &globalPos);

private:
    /**
     * @brief Create the owned three-pane splitter, summary and action buttons once.
     * @details Requires uninitialized widget members; wires Apply to focus-flush/refresh, OK to
     * close after flushing and Close directly to close. No new group or authoring setter is
     * called here.
     */
    void buildUi();
    /**
     * @brief Build the group filter/list and New/Delete/Rename controls.
     * @return New dialog-owned host widget, adopted by the splitter; no independent deletion by
     * callers.
     * @details Binds the existing proxy with single selection and inline rename triggers;
     * connects list selection to shared-table rebinding.
     */
    QWidget *buildLeftPane();
    /**
     * @brief Build the shared-model RTK, initial-abstraction and decay table views.
     * @return New dialog-owned host widget adopted by the splitter.
     * @details Adds gage/season controls and explanatory units: T hours, R/K dimensionless, IA
     * project rain-depth units and recovery depth/day; decay thermal terms retain Celsius
     * conventions. Views borrow layer-owned models and edits publish live.
     */
    QWidget *buildMiddlePane();
    /**
     * @brief Build the owned hydrograph chart, hover label and style/navigation actions.
     * @return New dialog-owned chart host adopted by the splitter; chart/view own the installed
     * series and axes.
     * @details Initializes three response areas and composite line, time-hours axis, formatting
     * controller and marker interactions. Does not query or advance an engine simulation.
     */
    QWidget *buildRightPane();
    /**
     * @brief Rebuild available rain-gage identities while suppressing live setters.
     * @details No-op without combo/layer/engine. Adds none then copies each non-null UTF-8 gage
     * ID, restoring previous display text if present. Leaves the first item selected when the
     * old choice disappears; no engine writes.
     */
    void populateGageCombo();
    /**
     * @brief Fill All and twelve month choices without invoking season-change work.
     * @details Stores -1 for All and 0..11 for January through December; clears any prior choice
     * and ends with All selected.
     */
    void populateSeasonCombo();
    /**
     * @brief Set the borrowed RTK/IA contexts to group/month and decay to group only.
     * @details Empty selection is passed through to available models. Does not create models or
     * directly write native rows; contexts are shared by other subscribers to these layer-owned
     * model instances.
     */
    void rebindModelsToCurrentSelection();
    /**
     * @brief Recompute response triangles and sampled composite from current authored RTK rows.
     * @details Reads selected group/month, preferring usable monthly values then usable All
     * values independently per response; requires R/K nonnegative and T positive, skipping
     * failed reads. Uses T hours, base T*(1+K), peak 2R/base and 201 composite samples; no
     * rainfall/runoff simulation or decay/IA effect is plotted. Resets axis extents and response
     * visibility, retaining styles. The separate legacy individual-peak extent expression uses
     * 2R/(K*T), so zero K can yield a non-finite extent; no additional finite-value guard is
     * applied here. No engine state changes.
     */
    void refreshPreview();
    /**
     * @brief Count authored rows/decay rows for the selection and display total group count.
     * @details Clears the label without a layer/engine; skips failed fixed-buffer row reads.
     * Counts all matching decay rows without separately checking an active flag; does not mutate
     * groups.
     */
    void updateGroupSummary();
    /**
     * @brief Move focus through table editors and then to the summary to request normal commits.
     * @details Uses indexWidget clearFocus when available, otherwise view focus, then a neutral
     * label. Delegates validation/publication to the models and does not aggregate success or
     * make several cell edits atomic.
     */
    void commitOpenEditors();
    /**
     * @brief Copy the proxy display identity for the current list index.
     * @return Empty without list/proxy/valid current index, otherwise the current row's
     * DisplayRole string; no engine lookup or ownership transfer.
     */
    QString currentGroupName() const;
    /**
     * @brief Read the selected season's stored month ordinal.
     * @return -1 without a combo or for All, otherwise 0..11 for January through December in the
     * initialized combo; does not change table context.
     */
    int currentMonth() const; ///< -1 for "All", 0..11 for months

    QPointer<SWMMModelLayer> m_layer;
    bool m_contextValid = true;

    // Layer-owned models (do not delete here — the layer owns them).
    HydrographGroupListModel *m_groupListModel = nullptr;
    HydrographRtkTableModel *m_rtkModel = nullptr;
    HydrographIaTableModel *m_iaModel = nullptr;
    HydrographDecayTableModel *m_decayModel = nullptr;

    // Filter proxy over the group list — owned by the dialog.
    QSortFilterProxyModel *m_filterProxy = nullptr;

    QSplitter *m_splitter = nullptr;

    // Left pane.
    QLineEdit *m_filterEdit = nullptr;
    QListView *m_groupList = nullptr;
    QToolButton *m_addBtn = nullptr;
    QToolButton *m_removeBtn = nullptr;
    QToolButton *m_renameBtn = nullptr;

    // Middle pane.
    QLabel *m_nameLabel = nullptr;
    QComboBox *m_gageCombo = nullptr;
    QComboBox *m_seasonCombo = nullptr;
    QTabWidget *m_tabs = nullptr;
    QTableView *m_rtkView = nullptr;
    QTableView *m_iaView = nullptr;
    QTableView *m_decayView = nullptr;
    QLabel *m_summaryLabel = nullptr; ///< bottom status strip

    // Right pane — UH preview plot.
    QChart *m_chart = nullptr;
    openswmmvis::ui::InteractiveChartView *m_chartView = nullptr;
    openswmmvis::ui::ChartAxisFormatController *m_axisFmt =
        nullptr; ///< Per-chart axis number format.
    QAreaSeries *m_areaSeries[3] = {nullptr, nullptr, nullptr};
    QLineSeries *m_areaUpper[3] = {nullptr, nullptr, nullptr};
    QLineSeries *m_sumSeries = nullptr;
    QValueAxis *m_xAxis = nullptr;
    QValueAxis *m_yAxis = nullptr;
    QLabel *m_hoverLabel = nullptr; ///< floats above plot

    // Action bar.
    QDialogButtonBox *m_buttonBox = nullptr;

    bool m_suppressGageSignal = false;
    bool m_suppressSeasonSignal = false;
};

#endif // HYDROGRAPHGROUPEDITOR_H
