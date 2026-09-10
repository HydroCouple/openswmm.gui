/*!
 * \file   performancepage.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "ui/dialogs/simoptions/performancepage.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

#include "core/preferencesmanager.h"
#include "ui/dialogs/simulationoptionsdialog.h"

namespace openswmmvis::ui
{

PerformancePage::PerformancePage(SimOptionsContext &ctx, QWidget *parent)
    : SimOptionsPage(ctx, parent)
{
    buildUi();
}

QString PerformancePage::title() const
{
    return tr("System / Performance");
}

void PerformancePage::setMinimumStepSetter(std::function<void(double)> setter)
{
    m_setMinimumStep = std::move(setter);
}

void PerformancePage::buildUi()
{
    auto *vlay = new QVBoxLayout(this);

    auto *threadsGroup = new QGroupBox(tr("Parallelisation"), this);
    auto *threadsForm  = new QFormLayout(threadsGroup);

    // Machine / OpenMP limits, queried once. The range stays 0–256 so the
    // user can deliberately oversubscribe; the suffix, tooltip and the
    // "Effective" label below show where the hardware limit is.
    swmm_get_thread_info(&m_threadInfo);

    m_threadsSpin = new QSpinBox(threadsGroup);
    m_threadsSpin->setRange(0, 256);
    m_threadsSpin->setSpecialValueText(tr("auto"));
    m_threadsSpin->setToolTip(
        tr("Number of OpenMP worker threads for the 1D and 2D solvers "
           "([OPTIONS] THREADS).\n"
           "0 = auto: the engine uses every logical processor the OpenMP "
           "runtime allows, then applies its own heuristics (model-size "
           "gates; on Apple Silicon the dynamic-wave team stays on the "
           "performance cores).\n"
           "N = exactly N threads. Values above the machine's logical "
           "processors are allowed but oversubscribe the CPU — the engine "
           "warns and the run is usually slower.\n\n%1")
            .arg(SimulationOptionsDialog::threadLimitsSummary(m_threadInfo)));
    tagOption(m_threadsSpin, "THREADS");
    threadsForm->addRow(tr("Wor&ker threads:"), m_threadsSpin);

    m_threadsEffective = new QLabel(threadsGroup);
    m_threadsEffective->setWordWrap(true);
    m_threadsEffective->setTextFormat(Qt::RichText);
    threadsForm->addRow(QString(), m_threadsEffective);
    connect(m_threadsSpin, qOverload<int>(&QSpinBox::valueChanged),
            this, &PerformancePage::refreshThreadsEffectiveLabel);
    refreshThreadsEffectiveLabel();

    // ── Fast preset ────────────────────────────────────────────────────
    // One-click speed recipe for 1D/2D-coupled runs: use all worker threads
    // and floor the adaptive step so the coupling can't collapse it. On the
    // Bellinge benchmark this is ~2.6x faster with BETTER mass balance than the
    // as-shipped run.
    auto *fastBtn = new QPushButton(tr("Apply fast preset"), threadsGroup);
    fastBtn->setToolTip(
        tr("Sets THREADS = %1 (this machine's performance cores) and "
           "MINIMUM_STEP = 1.0 s — the conservative fast\n"
           "recipe for 1D/2D-coupled models (~2.6x faster, and mass balance as\n"
           "good as or better than the default). For an ~4x quick-screening run\n"
           "raise MINIMUM_STEP to 2.0 s, but note its continuity degrades.")
            .arg(SimulationOptionsDialog::fastPresetThreads()));
    connect(fastBtn, &QPushButton::clicked, this, [this]() {
        int    threads = 8;
        double minStep = 1.5;
        SimulationOptionsDialog::fastPresetValues(threads, minStep);
        if (m_threadsSpin) m_threadsSpin->setValue(threads);
        // MINIMUM_STEP lives on Routing & Hydraulics; the dialog supplies the
        // setter so this page never reaches across into another page's widget.
        if (m_setMinimumStep) m_setMinimumStep(minStep);
        QMessageBox::information(
            this, tr("Fast preset applied"),
            tr("THREADS set to %1 and MINIMUM_STEP to 1.0 s.\n\n"
               "This is the conservative fast recipe for 1D/2D-coupled runs "
               "(~2.6x faster, with mass balance as good as or better than the "
               "default). Click OK / Apply to commit.").arg(threads));
    });
    threadsForm->addRow(QString(), fastBtn);

    auto *note = new QLabel(
        tr("<i>The IGNORE_* skip-process flags live on the Models / Processes tab. "
           "Future slices add more performance knobs here.</i>"),
        threadsGroup);
    note->setWordWrap(true);
    threadsForm->addRow(note);

    vlay->addWidget(threadsGroup);
    vlay->addStretch();
}

void PerformancePage::refreshThreadsEffectiveLabel()
{
    if (!m_threadsSpin || !m_threadsEffective) return;
    const int req = m_threadsSpin->value();

    // Suffix shows the hardware limit next to the value.
    const int logical = m_threadInfo.logical_cpus;
    const bool over = logical > 0 && req > logical;
    if (req == 0)
        m_threadsSpin->setSuffix(QString());
    else if (logical > 0)
        m_threadsSpin->setSuffix(over ? tr(" / %1 logical — oversubscribed").arg(logical)
                                      : tr(" / %1 logical").arg(logical));
    else
        m_threadsSpin->setSuffix(QString());

    // Effective counts come from the engine so this never re-implements
    // its heuristics; the engine reports 0 for a module the model lacks.
    int g = 0, dw = 0, td = 0;
    QString text;
    if (ctx_.engine()
        && swmm_get_effective_threads(ctx_.engine(), req, &g, &dw, &td) == SWMM_OK) {
        QStringList parts;
        parts << tr("general %1").arg(g);
        if (dw > 0) parts << tr("dynamic wave %1").arg(dw);
        if (td > 0) parts << tr("2D %1").arg(td);
        text = tr("Effective threads: %1.").arg(parts.join(QStringLiteral(" · ")));
    }
    if (over) {
        // Not-colour-alone: glyph + text carry the warning as well as colour.
        text += tr(" <span style=\"color:#D06F00\">&#9888; %1 threads exceed the "
                   "%2 logical processors — the run will be oversubscribed and "
                   "is usually slower.</span>").arg(req).arg(logical);
    } else if (m_threadInfo.perf_cores > 0 && req > m_threadInfo.perf_cores) {
        text += tr(" <span style=\"color:#D06F00\">&#9888; above the %1 "
                   "performance cores — efficiency cores slow the "
                   "barrier-synchronised solvers.</span>")
                    .arg(m_threadInfo.perf_cores);
    }
    m_threadsEffective->setText(text);
}

void PerformancePage::read()
{
    if (!m_threadsSpin) return;
    const auto sim = PreferencesManager::instance()->simulationDefaults();
    bool ok = false;
    const QString raw = ctx_.option("THREADS");
    const int v = raw.toInt(&ok);
    m_threadsSpin->setValue(ok ? v : sim.threads);
}

int PerformancePage::write()
{
    if (!m_threadsSpin) return 0;
    return ctx_.writeIfChanged("THREADS", QString::number(m_threadsSpin->value()));
}

} // namespace openswmmvis::ui
