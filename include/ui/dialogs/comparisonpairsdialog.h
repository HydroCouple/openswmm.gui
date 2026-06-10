/*!
 * \file   comparisonpairsdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  COMPARISON_PLOT_1V1_AND_TREE_PLAN Phase 5 — editor for the
 *         Comparison Plot's user-configured 1v1 pairs.
 *
 * MVC: the dialog edits ComparisonPlotModel's pair list directly; the
 * model emits `pairsChanged` on every edit, so the owning
 * ComparisonPlotDialog rebuilds its scatter column live while this
 * dialog is open. An empty pair list means auto mode (baseline vs every
 * other run, matched by objectRef).
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_COMPARISONPAIRSDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_COMPARISONPAIRSDIALOG_H

#include <QDialog>

class QComboBox;
class QListWidget;
class QPushButton;

namespace openswmmvis::plot { class ComparisonPlotModel; }

namespace openswmmvis::ui {

class ComparisonPairsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ComparisonPairsDialog(openswmmvis::plot::ComparisonPlotModel *model,
                                   QWidget *parent = nullptr);

private slots:
    void onAddClicked();
    void onRemoveClicked();
    void onResetClicked();

private:
    void buildUi();
    /*! \brief Repopulate the pair list widget from the model. */
    void refreshPairList();
    /*! \brief "<run> — <object> (<attr>)" label for a series index. */
    QString seriesLabel(int seriesIndex) const;

    openswmmvis::plot::ComparisonPlotModel *m_model = nullptr;

    QComboBox   *m_xCombo     = nullptr;
    QComboBox   *m_yCombo     = nullptr;
    QListWidget *m_pairList   = nullptr;
    QPushButton *m_addBtn     = nullptr;
    QPushButton *m_removeBtn  = nullptr;
    QPushButton *m_resetBtn   = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_COMPARISONPAIRSDIALOG_H
