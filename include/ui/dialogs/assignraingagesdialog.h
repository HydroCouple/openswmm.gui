/*!
 * \file   assignraingagesdialog.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Bind rain gages to subcatchments and RDII inflows spatially, in one undoable
 * step.
 *
 * Two methods:
 *
 *   Proximity — assign the gage whose Thiessen (Voronoi) cell covers the
 *     largest share of the subcatchment's area (an RDII node, being a point,
 *     takes its nearest gage). Uses the real watershed boundary rather than a
 *     representative point.
 *
 *   Interpolated — area-average natural-neighbour weights over the gage
 *     network, group objects whose weight vectors agree, and materialise each
 *     group as a generated gage backed by a generated series. SWMM binds
 *     exactly one gage per subcatchment / unit-hydrograph group, so an
 *     interpolated field cannot be expressed any other way.
 *
 * The dialog is a front end over assignment/raingageassignment.h: inputs are
 * snapshotted on the GUI thread, the plan is computed on a worker (progress +
 * Cancel), and a computed plan is reused by Apply until the model or an option
 * changes. Everything is computed before any mutation, so the volume gate can
 * abort with nothing written; the whole assignment is one undo step.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_ASSIGNRAINGAGESDIALOG_H
#define OPENSWMMVIS_UI_DIALOGS_ASSIGNRAINGAGESDIALOG_H

#include "assignment/raingageassignment.h"

#include <QDialog>
#include <QFutureWatcher>
#include <QPointer>

#include <memory>

class MapCanvas;
class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QRadioButton;
class QTableWidget;
class QTimer;
class SelectionManager;
class SWMMModelLayer;

namespace openswmmvis::ui {

class AssignRainGagesDialog : public QDialog
{
    Q_OBJECT

public:
    AssignRainGagesDialog(SWMMModelLayer   *layer,
                          MapCanvas        *canvas,
                          SelectionManager *selection,
                          QWidget          *parent = nullptr);
    ~AssignRainGagesDialog() override;

    /*! \brief True while a plan is being computed. Tests and the perf harness
     *         wait on this after clicking Preview / Apply. */
    [[nodiscard]] bool busy() const;

private slots:
    void onMethodChanged();
    void onPreview();
    void onApply();
    void onComputeFinished();
    void onCancelClicked();
    void invalidatePlan();

private:
    void buildUi();
    void updateButtons();

    [[nodiscard]] assignment::raingage::Options currentOptions() const;
    void startCompute(bool applyWhenDone);
    void applyPlan(const assignment::raingage::Plan &plan);
    void showPlan(const assignment::raingage::Plan &plan, bool applied);

    QPointer<SWMMModelLayer>   m_layer;
    QPointer<MapCanvas>        m_canvas;
    QPointer<SelectionManager> m_selection;

    QRadioButton   *m_methodProximity = nullptr;
    QRadioButton   *m_methodInterp    = nullptr;
    QComboBox      *m_variantCombo    = nullptr;
    QDoubleSpinBox *m_tolSpin         = nullptr;
    QCheckBox      *m_targetSubcatch  = nullptr;
    QCheckBox      *m_targetRdii      = nullptr;
    QRadioButton   *m_scopeAll        = nullptr;
    QRadioButton   *m_scopeSelected   = nullptr;
    QLabel         *m_gageCountLbl    = nullptr;
    QTableWidget   *m_preview         = nullptr;
    QLabel         *m_statusLbl       = nullptr;
    QProgressBar   *m_progressBar     = nullptr;
    QPushButton    *m_previewBtn      = nullptr;
    QPushButton    *m_applyBtn        = nullptr;
    QPushButton    *m_cancelBtn       = nullptr;

    // ── Background compute + plan cache ─────────────────────────────────
    QFutureWatcher<assignment::raingage::Plan> m_watcher;
    std::shared_ptr<assignment::raingage::Progress> m_progress;
    QTimer *m_progressTimer = nullptr;
    bool    m_applyWhenDone = false;
    bool    m_computing     = false;   ///< Until onComputeFinished() has run.
    bool    m_havePlan      = false;   ///< m_plan matches the current model + options.
    quint64 m_generation    = 0;       ///< Bumped by every invalidating change.
    quint64 m_computeGeneration = 0;
    assignment::raingage::Plan m_plan;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_ASSIGNRAINGAGESDIALOG_H
