/*!
 * \file   performancepage.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Simulation Options → System / Performance.
 *
 * Parallelisation (`THREADS`) plus the one-click fast preset. The preset also
 * sets `MINIMUM_STEP`, which lives on Routing & Hydraulics, so the page is
 * handed a setter rather than reaching across pages or going through the
 * context (PLAN §4.1).
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
    explicit PerformancePage(SimOptionsContext &ctx, QWidget *parent = nullptr);

    [[nodiscard]] QString title() const override;
    void read() override;
    int  write() override;

    /*!
     * \brief Where the fast preset sends its MINIMUM_STEP value.
     *
     * Wired by the dialog: to the inline hydraulics spin while that page is
     * still part of the monolith, and to HydraulicsPage::setMinimumStep once
     * T4 has extracted it. A default-constructed setter is a no-op, so the
     * preset degrades to threads-only rather than crashing.
     */
    void setMinimumStepSetter(std::function<void(double)> setter);

private slots:
    void refreshThreadsEffectiveLabel();

private:
    void buildUi();

    QSpinBox *m_threadsSpin      = nullptr;
    QLabel   *m_threadsEffective = nullptr;
    SWMM_ThreadInfo m_threadInfo{};                 ///< Machine / OpenMP limits, filled once.
    std::function<void(double)> m_setMinimumStep;
};

} // namespace openswmmvis::ui

#endif // PERFORMANCEPAGE_H
