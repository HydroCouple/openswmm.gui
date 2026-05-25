/*!
 * \file   hydrographgroupeditor.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Slice BS Phase 6.9.2 — Unit-hydrograph editor dialog.
 *
 * Three-pane non-modal editor for [HYDROGRAPHS] / [RDII_DECAY] data:
 *   - Pane 1: groups list (QListView bound to HydrographGroupListModel)
 *             with a toolbar of New / Delete / Rename actions and a
 *             search field over a QSortFilterProxyModel.
 *   - Pane 2: name label + rain-gage combo + season combo + tab widget
 *             with two QTableViews (RTK + Initial Abstraction). Both
 *             tables are bound to HydrographRtkTableModel /
 *             HydrographIaTableModel + HydrographDecayTableModel.
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
namespace QtCharts {}  // forward
QT_END_NAMESPACE

class QChart;
class QAreaSeries;
class QLineSeries;
class QValueAxis;
class QLegendMarker;
namespace openswmmvis::ui { class InteractiveChartView; }

class HydrographGroupEditor : public QDialog
{
    Q_OBJECT
public:
    explicit HydrographGroupEditor(SWMMModelLayer *layer,
                                    QWidget *parent = nullptr);
    ~HydrographGroupEditor() override;

    /*! Select \a name in the groups list and make sure the dialog is
     *  visible / raised. No-op if the group doesn't exist. */
    void openForGroup(const QString &name);

public slots:
    /*! Slice BM.0-Add-New — bring the dialog forward and trigger the
     *  same name-prompt + create flow the left-pane "New" button drives.
     *  External Add-New entrypoints (Object Browser Data section, future
     *  Data menu) call this instead of poking m_addBtn directly. */
    void beginNewGroup();

    /*! Run the editor as a modal "pick a unit hydrograph" dialog. Opens
     *  pre-selected on \a initialName (or the first group if empty), lets
     *  the user create / edit / pick a group with full MVC sync, then
     *  returns the currently-highlighted group's name on accept (Apply,
     *  OK, or Close). Returns empty string if the user dismissed without
     *  any selection. Use this from the RDII picker's browse button. */
    static QString pickGroup(SWMMModelLayer *layer,
                              const QString &initialName,
                              QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *e) override;
    void showEvent(QShowEvent  *e) override;

private slots:
    void onGroupSelectionChanged();
    void onSeasonChanged(int index);
    void onGageChanged(int index);
    void onNewGroup();
    void onDeleteGroup();
    void onRenameGroup();
    void onHydrographChanged(const QString &uhName);
    void onFilterTextChanged(const QString &text);
    void onApplyClicked();
    void onOkClicked();
    void onLegendMarkerClicked();
    void onPlotHover(const QPointF &point, bool state);
    void showPlotStyleMenu(const QPoint &globalPos);

private:
    void buildUi();
    QWidget *buildLeftPane();
    QWidget *buildMiddlePane();
    QWidget *buildRightPane();
    void    populateGageCombo();
    void    populateSeasonCombo();
    void    rebindModelsToCurrentSelection();
    void    refreshPreview();
    void    updateGroupSummary();
    void    commitOpenEditors();
    QString currentGroupName() const;
    int     currentMonth() const;       ///< -1 for "All", 0..11 for months
    void    saveState();
    void    restoreState();

    SWMMModelLayer            *m_layer = nullptr;

    // Layer-owned models (do not delete here — the layer owns them).
    HydrographGroupListModel  *m_groupListModel = nullptr;
    HydrographRtkTableModel   *m_rtkModel       = nullptr;
    HydrographIaTableModel    *m_iaModel        = nullptr;
    HydrographDecayTableModel *m_decayModel     = nullptr;

    // Filter proxy over the group list — owned by the dialog.
    QSortFilterProxyModel     *m_filterProxy    = nullptr;

    QSplitter   *m_splitter        = nullptr;

    // Left pane.
    QLineEdit   *m_filterEdit      = nullptr;
    QListView   *m_groupList       = nullptr;
    QToolButton *m_addBtn          = nullptr;
    QToolButton *m_removeBtn       = nullptr;
    QToolButton *m_renameBtn       = nullptr;

    // Middle pane.
    QLabel      *m_nameLabel       = nullptr;
    QComboBox   *m_gageCombo       = nullptr;
    QComboBox   *m_seasonCombo     = nullptr;
    QTabWidget  *m_tabs            = nullptr;
    QTableView  *m_rtkView         = nullptr;
    QTableView  *m_iaView          = nullptr;
    QTableView  *m_decayView       = nullptr;
    QLabel      *m_summaryLabel    = nullptr;     ///< bottom status strip

    // Right pane — UH preview plot.
    QChart                                *m_chart       = nullptr;
    openswmmvis::ui::InteractiveChartView *m_chartView   = nullptr;
    QAreaSeries                           *m_areaSeries[3] = {nullptr, nullptr, nullptr};
    QLineSeries                           *m_areaUpper[3]  = {nullptr, nullptr, nullptr};
    QLineSeries                           *m_sumSeries  = nullptr;
    QValueAxis                            *m_xAxis      = nullptr;
    QValueAxis                            *m_yAxis      = nullptr;
    QLabel                                *m_hoverLabel = nullptr;  ///< floats above plot

    // Action bar.
    QDialogButtonBox *m_buttonBox = nullptr;

    bool m_suppressGageSignal   = false;
    bool m_suppressSeasonSignal = false;
};

#endif // HYDROGRAPHGROUPEDITOR_H
