/**
 * @file      comparisonpairsdialog.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     COMPARISON_PLOT_1V1_AND_TREE_PLAN Phase 5 — editor for the
 *         Comparison Plot's user-configured 1v1 pairs.
 *
 * MVC: the dialog edits ComparisonPlotModel's pair list directly; the
 * model emits `pairsChanged` on every edit, so the owning
 * ComparisonPlotDialog rebuilds its scatter column live while this
 * dialog is open. An empty pair list means auto mode (baseline vs every
 * other run, matched by objectRef).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_COMPARISONPAIRSDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_COMPARISONPAIRSDIALOG_H

#include <QDialog>

class QComboBox;
class QListWidget;
class QPushButton;

namespace openswmmvis::plot
{
class ComparisonPlotModel;
}

namespace openswmmvis::ui
{

class ComparisonPairsDialog : public QDialog
{
    Q_OBJECT
public:
    /**
     * @param[in] model Optional borrowed raw comparison model; must outlive dialog and every
     *   callback/modal error loop. Null gives inert empty form.
     * @param[in] parent Optional QWidget owner; caller manages parentless dialog.
     * @brief Snapshot series choices and display model pair list, with edits applied live to shared
     *   model.
     * @note Close/reject does not undo pair changes. No subscriptions update series combos/pair
     *   list after external model edits; caller keeps source indices stable while open. GUI-thread
     *   only.
     */
    explicit ComparisonPairsDialog(openswmmvis::plot::ComparisonPlotModel *model,
                                   QWidget *parent = nullptr);

private slots:
    /**
     * @brief Submit current directed X/Y series-index pair to shared model and refresh list on
     *   success.
     * @note Null model/empty X combo no-op. Model rejects invalid/self/same-directed-duplicate or
     *   differing descriptor/species pairs; rejection shows modal information. Reversed pair
     *   remains distinct. Successful add emits model pairsChanged; no undo, file/settings save or
     *   staged acceptance.
     */
    void onAddClicked();
    /**
     * @brief Remove currently selected displayed pair indices bottom-up, then rebuild list.
     * @note Null model no-op; multiple successful removals emit pairsChanged individually. List
     *   indices must correspond to current shared model pairs, since external edits are not
     *   followed automatically. No confirmation/rollback/undo.
     */
    void onRemoveClicked();
    /**
     * @brief Clear shared model configured pairs immediately and refresh list, restoring host
     *   auto-pair mode.
     * @note Null model no-op; already empty model emits no pair-change signal. Close does not
     *   revert reset. No baseline/source deletion or settings save here.
     */
    void onResetClicked();

private:
    /**
     * @brief Create owned X/Y combo snapshots of all current model series and live pair
     *   Add/Remove/Reset controls with Close button.
     * @note No filtering of incompatible combo choices, model performs rejection on Add. Empty
     *   model still gets enabled buttons but handlers are inert. No external-model subscriptions or
     *   engine reads.
     */
    void buildUi();
    /*! \brief Repopulate the pair list widget from the model.
     * @brief Clear displayed pair rows and rebuild labels from current shared model pair indices;
     *   null leaves empty.
     * @note Clears selection, does not update series combos or mutate pairs. Model must keep every
     *   pair series index valid; no local bounds recovery in seriesLabel.
 */
    void refreshPairList();
    /*! \brief "<run> — <object> (<attr>)" label for a series index.
     * @param[in] seriesIndex Required current index in [0, model.seriesCount); nonnull model and
     *   valid spec required, no range guard here.
     * @return Legend override when nonempty, otherwise run label, object name (Mesh2D uses raw
     *   zero-based cell index) and PlotAttribute label. Unknown run index yields explanatory text;
     *   species descriptor distinction is not explicitly appended. No data extraction, unit
     *   conversion or model mutation.
 */
    QString seriesLabel(int seriesIndex) const;

    openswmmvis::plot::ComparisonPlotModel *m_model = nullptr;

    QComboBox *m_xCombo = nullptr;
    QComboBox *m_yCombo = nullptr;
    QListWidget *m_pairList = nullptr;
    QPushButton *m_addBtn = nullptr;
    QPushButton *m_removeBtn = nullptr;
    QPushButton *m_resetBtn = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_COMPARISONPAIRSDIALOG_H
