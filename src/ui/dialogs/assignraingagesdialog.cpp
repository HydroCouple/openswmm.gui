/*!
 * \file   assignraingagesdialog.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date 2026
 */

#include "ui/dialogs/assignraingagesdialog.h"

#include "layers/swmmmodellayer.h"
#include "map/mapcanvas.h"
#include "map/mapundostack.h"
#include "selection/selectionmanager.h"
#include "ui/dialogs/dialoglayoutpersistence.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_inflows.h>

#include <utility>

namespace openswmmvis::ui {

namespace rg = openswmmvis::assignment::raingage;

// ===========================================================================
// Construction
// ===========================================================================

AssignRainGagesDialog::AssignRainGagesDialog(SWMMModelLayer   *layer,
                                             MapCanvas        *canvas,
                                             SelectionManager *selection,
                                             QWidget          *parent)
    : QDialog(parent)
    , m_layer(layer)
    , m_canvas(canvas)
    , m_selection(selection)
{
    setObjectName(QStringLiteral("AssignRainGagesDialog"));
    setWindowTitle(tr("Assign Rain Gages"));
    buildUi();

    connect(&m_watcher, &QFutureWatcher<rg::Plan>::finished,
            this, &AssignRainGagesDialog::onComputeFinished);
    m_progressTimer = new QTimer(this);
    m_progressTimer->setInterval(100);
    connect(m_progressTimer, &QTimer::timeout, this, [this]() {
        if (!m_progress) return;
        const int total = m_progress->total.load();
        m_progressBar->setRange(0, std::max(1, total));
        m_progressBar->setValue(std::min(total, m_progress->done.load()));
    });

    // Any model change makes a computed plan stale.
    if (m_layer)
    {
        connect(m_layer, &SWMMModelLayer::modelEdited,
                this, &AssignRainGagesDialog::invalidatePlan);
        connect(m_layer, &SWMMModelLayer::geometryChanged,
                this, &AssignRainGagesDialog::invalidatePlan);
        connect(m_layer, &SWMMModelLayer::attributeChanged,
                this, &AssignRainGagesDialog::invalidatePlan);
        connect(m_layer, &SWMMModelLayer::hydrographChanged,
                this, &AssignRainGagesDialog::invalidatePlan);
        connect(m_layer, &SWMMModelLayer::dataObjectsChanged,
                this, &AssignRainGagesDialog::invalidatePlan);
    }

    onMethodChanged();
    updateButtons();
    applyAlwaysOnTopPolicy(this);
}

AssignRainGagesDialog::~AssignRainGagesDialog()
{
    if (m_progress) m_progress->cancel = true;
    m_watcher.waitForFinished();
}

void AssignRainGagesDialog::buildUi()
{
    auto *outer = new QVBoxLayout(this);

    // ── Method ──────────────────────────────────────────────────────────
    {
        auto *group = new QGroupBox(tr("Method"), this);
        auto *v = new QVBoxLayout(group);

        m_methodProximity = new QRadioButton(tr("Nearest gage (Thiessen area majority)"), group);
        m_methodProximity->setToolTip(
            tr("Assign the gage whose Thiessen polygon covers the largest share of "
               "each subcatchment's area; an RDII node takes its nearest gage. "
               "Creates no gages."));
        m_methodProximity->setChecked(true);
        v->addWidget(m_methodProximity);

        m_methodInterp = new QRadioButton(
            tr("Natural-neighbour interpolation (creates gages)"), group);
        m_methodInterp->setToolTip(
            tr("Area-average interpolation weights over the gage network, then "
               "create one gage and time series per distinct weight vector. "
               "SWMM allows only one gage per subcatchment, so an interpolated "
               "field has to be materialised this way."));
        v->addWidget(m_methodInterp);

        auto *form = new QFormLayout;
        m_variantCombo = new QComboBox(group);
        m_variantCombo->addItem(tr("Sibson (area stealing)"), 0);
        m_variantCombo->addItem(tr("Laplace (Voronoi facet)"), 1);
        form->addRow(tr("Weighting:"), m_variantCombo);

        m_tolSpin = new QDoubleSpinBox(group);
        m_tolSpin->setRange(0.1, 10.0);
        m_tolSpin->setSingleStep(0.5);
        m_tolSpin->setValue(1.0);
        m_tolSpin->setSuffix(tr(" %"));
        m_tolSpin->setToolTip(
            tr("Objects whose interpolation weights agree within this tolerance "
               "share one generated gage. Larger values create fewer objects."));
        form->addRow(tr("Group weights within:"), m_tolSpin);
        v->addLayout(form);

        outer->addWidget(group);
    }

    // ── Targets ─────────────────────────────────────────────────────────
    {
        auto *group = new QGroupBox(tr("Assign gages to"), this);
        auto *v = new QVBoxLayout(group);
        m_targetSubcatch = new QCheckBox(tr("Subcatchments"), group);
        m_targetSubcatch->setChecked(true);
        v->addWidget(m_targetSubcatch);

        const int nRdii = (m_layer && m_layer->engine())
                              ? swmm_rdii_count(m_layer->engine()) : 0;
        m_targetRdii = new QCheckBox(
            tr("RDII inflows (%n node entry/entries)", nullptr, std::max(0, nRdii)), group);
        m_targetRdii->setToolTip(
            tr("SWMM attaches the rain gage to the unit hydrograph group, not the "
               "node. A group whose nodes fall under different gages is split: it "
               "keeps the gage with the most sewer area, and a copy named "
               "<group>_<gage> serves each other gage."));
        m_targetRdii->setChecked(nRdii > 0);
        m_targetRdii->setEnabled(nRdii > 0);
        v->addWidget(m_targetRdii);
        outer->addWidget(group);
    }

    // ── Scope ───────────────────────────────────────────────────────────
    {
        auto *group = new QGroupBox(tr("Apply to"), this);
        auto *v = new QVBoxLayout(group);
        m_scopeAll = new QRadioButton(tr("All objects"), group);
        m_scopeAll->setChecked(true);
        v->addWidget(m_scopeAll);

        int nSelected = 0;
        if (m_selection)
            for (const SWMMObjectRef &ref : m_selection->selection())
                if (ref.objectType == SWMMObjectRef::Subcatchment
                    || ref.objectType == SWMMObjectRef::Node)
                    ++nSelected;
        m_scopeSelected = new QRadioButton(
            tr("Selected subcatchments and nodes (%1)").arg(nSelected), group);
        m_scopeSelected->setEnabled(nSelected > 0);
        v->addWidget(m_scopeSelected);

        outer->addWidget(group);
    }

    m_gageCountLbl = new QLabel(this);
    m_gageCountLbl->setWordWrap(true);
    outer->addWidget(m_gageCountLbl);

    // ── Preview ─────────────────────────────────────────────────────────
    m_preview = new QTableWidget(this);
    m_preview->setObjectName(QStringLiteral("AssignRainGagesPreview"));
    m_preview->setColumnCount(5);
    m_preview->setHorizontalHeaderLabels(
        {tr("Object"), tr("Type"), tr("Current gage"), tr("New gage"), tr("Detail")});
    m_preview->horizontalHeader()->setStretchLastSection(false);
    m_preview->verticalHeader()->setVisible(false);
    m_preview->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_preview->setSelectionBehavior(QAbstractItemView::SelectRows);
    outer->addWidget(m_preview, 1);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setVisible(false);
    outer->addWidget(m_progressBar);

    m_statusLbl = new QLabel(tr("Choose a method, then Preview."), this);
    m_statusLbl->setWordWrap(true);
    outer->addWidget(m_statusLbl);

    auto *buttons = new QDialogButtonBox(this);
    m_previewBtn = buttons->addButton(tr("Preview"), QDialogButtonBox::ActionRole);
    m_applyBtn   = buttons->addButton(tr("Apply"),   QDialogButtonBox::AcceptRole);
    m_cancelBtn  = buttons->addButton(tr("Cancel"),  QDialogButtonBox::ActionRole);
    m_cancelBtn->setToolTip(tr("Stop the computation in progress"));
    buttons->addButton(QDialogButtonBox::Close);
    connect(m_previewBtn, &QPushButton::clicked, this, &AssignRainGagesDialog::onPreview);
    connect(m_applyBtn,   &QPushButton::clicked, this, &AssignRainGagesDialog::onApply);
    connect(m_cancelBtn,  &QPushButton::clicked, this, &AssignRainGagesDialog::onCancelClicked);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    outer->addWidget(buttons);

    connect(m_methodProximity, &QRadioButton::toggled,
            this, &AssignRainGagesDialog::onMethodChanged);
    // Every option feeds the plan.
    for (QAbstractButton *b : {static_cast<QAbstractButton *>(m_methodProximity),
                               static_cast<QAbstractButton *>(m_targetSubcatch),
                               static_cast<QAbstractButton *>(m_targetRdii),
                               static_cast<QAbstractButton *>(m_scopeAll)})
        connect(b, &QAbstractButton::toggled, this, &AssignRainGagesDialog::invalidatePlan);
    connect(m_variantCombo, &QComboBox::currentIndexChanged,
            this, &AssignRainGagesDialog::invalidatePlan);
    connect(m_tolSpin, &QDoubleSpinBox::valueChanged,
            this, &AssignRainGagesDialog::invalidatePlan);

    resize(760, 600);
}

void AssignRainGagesDialog::onMethodChanged()
{
    const bool interp = m_methodInterp && m_methodInterp->isChecked();
    if (m_variantCombo) m_variantCombo->setEnabled(interp);
    if (m_tolSpin)      m_tolSpin->setEnabled(interp);

    QStringList unlocated, coincident;
    const QVector<rg::GageSite> gages = rg::eligibleGages(m_layer, &unlocated, &coincident);
    QStringList notes;
    if (!unlocated.isEmpty())
        notes << tr("%n gage(s) have no map location and were excluded.", nullptr,
                    int(unlocated.size()));
    if (!coincident.isEmpty())
        notes << tr("%n gage(s) share a location with another and were excluded: %1.",
                    nullptr, int(coincident.size()))
                     .arg(coincident.join(QStringLiteral(", ")));
    m_gageCountLbl->setText(tr("%n usable rain gage(s).", nullptr, int(gages.size()))
                            + (notes.isEmpty() ? QString()
                                               : QStringLiteral(" ") + notes.join(QLatin1Char(' '))));
    updateButtons();
}

void AssignRainGagesDialog::updateButtons()
{
    const bool ready = m_layer && m_layer->engine();
    const bool running = busy();
    if (m_previewBtn) m_previewBtn->setEnabled(ready && !running);
    if (m_applyBtn)   m_applyBtn->setEnabled(ready && !running);
    if (m_cancelBtn)  m_cancelBtn->setEnabled(running);
}

bool AssignRainGagesDialog::busy() const
{
    return m_computing;
}

void AssignRainGagesDialog::invalidatePlan()
{
    m_havePlan = false;
    ++m_generation;
}

// ===========================================================================
// Compute
// ===========================================================================

rg::Options AssignRainGagesDialog::currentOptions() const
{
    rg::Options o;
    o.method = (m_methodInterp && m_methodInterp->isChecked()) ? rg::Method::Interpolated
                                                               : rg::Method::Nearest;
    o.laplace = m_variantCombo && m_variantCombo->currentData().toInt() == 1;
    o.tolerance = (m_tolSpin ? m_tolSpin->value() : 1.0) / 100.0;
    o.subcatchments = m_targetSubcatch && m_targetSubcatch->isChecked();
    o.rdii = m_targetRdii && m_targetRdii->isChecked() && m_targetRdii->isEnabled();
    o.selectedOnly = m_scopeSelected && m_scopeSelected->isChecked() && m_selection;
    if (o.selectedOnly)
        for (const SWMMObjectRef &ref : m_selection->selection())
        {
            if (ref.objectType == SWMMObjectRef::Subcatchment)
                o.selectedSubcatchments << ref.name;
            else if (ref.objectType == SWMMObjectRef::Node)
                o.selectedNodes << ref.name;
        }
    return o;
}

void AssignRainGagesDialog::startCompute(bool applyWhenDone)
{
    if (busy()) return;
    m_applyWhenDone = applyWhenDone;
    m_havePlan = false;
    m_computing = true;
    m_computeGeneration = m_generation;

    // Inputs are read on the GUI thread (engine access); only the pure plan
    // runs on the worker.
    auto input = std::make_shared<rg::Input>(rg::gatherInput(m_layer, currentOptions()));
    m_progress = std::make_shared<rg::Progress>();
    auto progress = m_progress;
    m_watcher.setFuture(QtConcurrent::run([input, progress]() {
        return rg::computePlan(*input, progress.get());
    }));

    m_statusLbl->setText(tr("Computing…"));
    m_progressBar->setRange(0, 0);
    m_progressBar->setVisible(true);
    m_progressTimer->start();
    updateButtons();
}

void AssignRainGagesDialog::onComputeFinished()
{
    m_progressTimer->stop();
    m_progressBar->setVisible(false);
    m_plan = m_watcher.result();
    // A change that landed while the worker ran makes this plan stale.
    m_havePlan = m_plan.error.isEmpty() && m_computeGeneration == m_generation;
    bool apply = std::exchange(m_applyWhenDone, false);
    if (apply && !m_havePlan && m_plan.error.isEmpty())
    {
        m_plan.error = tr("The model changed while the assignment was being "
                          "computed. Nothing was applied; run it again.");
        apply = false;
    }
    m_computing = false;
    updateButtons();
    if (apply && m_havePlan)
        applyPlan(m_plan);
    else
        showPlan(m_plan, false);
}

void AssignRainGagesDialog::onCancelClicked()
{
    if (m_progress) m_progress->cancel = true;
}

void AssignRainGagesDialog::onPreview()
{
    startCompute(false);
}

void AssignRainGagesDialog::onApply()
{
    // A preview of the current model and options is reused rather than
    // recomputed; anything that changed since then cleared m_havePlan.
    if (m_havePlan)
        applyPlan(m_plan);
    else
        startCompute(true);
}

// ===========================================================================
// Apply + presentation
// ===========================================================================

void AssignRainGagesDialog::applyPlan(const rg::Plan &plan)
{
    const QString text = (m_methodInterp && m_methodInterp->isChecked())
                             ? tr("Assign Rain Gages (Interpolated)")
                             : tr("Assign Rain Gages (Nearest)");
    QUndoCommand *cmd = rg::makeApplyCommand(m_layer, m_canvas, plan, text);
    if (!cmd)
    {
        showPlan(plan, false);
        m_statusLbl->setText(plan.error.isEmpty() ? tr("Nothing to assign.") : plan.error);
        return;
    }
    // With no stack (headless / detached canvas) the command still has to run;
    // same fallback mesh::pushCellParamEdits uses.
    if (QUndoStack *stack = m_canvas ? m_canvas->undoStack() : nullptr)
        stack->push(cmd);
    else
    {
        cmd->redo();
        delete cmd;
    }
    m_havePlan = false;   // the model just changed
    showPlan(plan, true);
}

void AssignRainGagesDialog::showPlan(const rg::Plan &plan, bool applied)
{
    m_preview->setRowCount(0);
    if (!plan.error.isEmpty())
    {
        m_statusLbl->setText(plan.error);
        return;
    }

    m_preview->setUpdatesEnabled(false);
    m_preview->setRowCount(static_cast<int>(plan.rows.size()));
    for (int r = 0; r < plan.rows.size(); ++r)
    {
        const rg::RowPlan &row = plan.rows[r];
        const QString type = row.kind == rg::RowPlan::Kind::Rdii ? tr("RDII")
                                                                 : tr("Subcatchment");
        const QStringList cells{row.object, type, row.oldGage, row.newGage, row.detail};
        for (int c = 0; c < cells.size(); ++c)
        {
            auto *item = new QTableWidgetItem(cells[c]);
            if (!row.changed)
                item->setForeground(palette().brush(QPalette::Disabled, QPalette::Text));
            m_preview->setItem(r, c, item);
        }
    }
    m_preview->resizeColumnsToContents();
    m_preview->setUpdatesEnabled(true);

    QStringList summary;
    summary << (applied ? tr("Assigned %1 of %2 object(s).").arg(plan.changed).arg(plan.scanned)
                        : tr("%1 of %2 object(s) would change.").arg(plan.changed).arg(plan.scanned));
    if (!plan.generated.isEmpty())
    {
        int created = 0, updated = 0;
        double worst = 0.0;
        for (const rg::GeneratedGage &g : plan.generated)
        {
            created += g.isNew ? 1 : 0;
            updated += g.isUpdate ? 1 : 0;
            worst = std::max(worst, g.relError);
        }
        summary << tr("%1 gage(s) created, %2 reused, %3 removed. Worst volume error %4.")
                       .arg(created).arg(updated).arg(plan.staleGages.size())
                       .arg(worst, 0, 'g', 3);
    }
    if (!plan.newGroups.isEmpty() || !plan.groupGages.isEmpty())
        summary << tr("%1 unit hydrograph group(s) re-gaged, %2 created.")
                       .arg(plan.groupGages.size()).arg(plan.newGroups.size());
    summary += plan.warnings;
    m_statusLbl->setText(summary.join(QStringLiteral(" ")));
}

} // namespace openswmmvis::ui
