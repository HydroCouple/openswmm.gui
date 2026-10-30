/**
 * @file      performancepage.h
 * @brief     Simulation Options → System / Performance.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Simulation Options → System / Performance.
 *
 * Parallelisation (`THREADS`) plus the one-click fast preset. The preset also
 * sets `MINIMUM_STEP`, which lives on Routing & Hydraulics, so the page is
 * handed a setter rather than reaching across pages or going through the
 * context (PLAN §4.1).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef PERFORMANCEPAGE_H
#define PERFORMANCEPAGE_H

#include <functional>

#include "ui/dialogs/simoptions/simoptionspage.h"

class QLabel;
class QSpinBox;

namespace openswmmvis::ui
{

class PerformancePage : public SimOptionsPage
{
    Q_OBJECT

public:
    /**
     * @brief Build worker-thread controls and the draft fast-preset action.
     * @param ctx Borrowed context that must outlive the page and provide valid handles
     * while querying effective threads or applying options.
     * @param parent Optional QWidget owner; null leaves lifetime to the caller.
     * Builds once and captures machine thread information; read() hydrates the draft.
     */
    explicit PerformancePage(SimOptionsContext &ctx, QWidget *parent = nullptr);

    /**
     * @brief Return the localized sidebar caption.
     * @return Owned System / Performance string.
     */
    [[nodiscard]] QString title() const override;
    /**
     * @brief Hydrate THREADS from the context or simulation preferences and clamp into the spin
     * range zero..256.
     * Missing editor is a no-op. Does not block value signals, so a changed value
     * refreshes effective-thread feedback; no engine setter is called.
     */
    void read() override;
    /**
     * @brief Attempt the current THREADS draft through context change detection.
     * @return Zero for missing editor/no-op/refusal, one for a successful setter.
     * Records the key through the context, without creating undo or saving files.
     */
    int write() override;

    /**
     * @brief Replace the retained cross-page fast-preset callback.
     * @param setter Moved callable receiving a minimum routing step in seconds. The
     * fast preset supplies 1.0; an empty callable leaves the preset threads-only.
     * Caller keeps captured objects live until replacement/page destruction. Called
     * synchronously on the GUI thread by the preset action; assignment does not apply
     * the value and callback exceptions are not caught.
     */
    void setMinimumStepSetter(std::function<void(double)> setter);

private slots:
    /**
     * @brief Refresh thread-limit suffix and engine-derived effective-team feedback from the
     * draft.
     * Uses the once-captured machine limits, flags logical/performance-core excess
     * and queries effective general/dynamic-wave/2D counts when an engine is present.
     * Failed engine queries omit those counts. Does not write THREADS or start a run.
     */
    void refreshThreadsEffectiveLabel();

private:
    /**
     * @brief Create owned worker-count, feedback and preset controls once.
     * Queries machine thread info without checking status, permits zero(auto)..256
     * including oversubscription and tags THREADS. The preset callback queries the
     * performance-core recipe, updates the clamped thread draft and invokes the stored
     * minimum-step setter when present, then shows a modal information message.
     * Neither action commits options; caller/dependencies must remain valid during
     * callbacks and nested prompts. This method does not establish a speed guarantee.
     */
    void buildUi();

    QSpinBox *m_threadsSpin = nullptr;
    QLabel *m_threadsEffective = nullptr;
    SWMM_ThreadInfo m_threadInfo{}; ///< Machine / OpenMP limits, filled once.
    std::function<void(double)> m_setMinimumStep;
};

} // namespace openswmmvis::ui

#endif // PERFORMANCEPAGE_H
