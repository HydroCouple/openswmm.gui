#ifndef RIBBONCOMPACTOR_H
#define RIBBONCOMPACTOR_H

/**
 * @file      ribboncompactor.h
 * @brief     UI redesign iteration 2 (R2/R5) — watches one group-bearing QToolBar and drives
 *            per-group RibbonMode from the solver whenever the bar resizes or first shows:
 *            trailing groups shed detail first, and promotions only land past a 32 px dead band
 *            (applyRibbonHysteresis) so a window dragged across a boundary doesn't flap.
 *
 * UI redesign iteration 2 (R2/R5) — watches one group-bearing QToolBar
 * and drives per-group RibbonMode from the solver whenever the bar
 * resizes or first shows: trailing groups shed detail first, and
 * promotions only land past a 32 px dead band (applyRibbonHysteresis)
 * so a window dragged across a boundary doesn't flap.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <QList>
#include <QObject>
#include <QPointer>
#include <QVector>

#include "ribbonlayoutsolver.h"

class QToolBar;

namespace openswmmvis::ui
{

class RibbonGroup;

class RibbonCompactor : public QObject
{
    Q_OBJECT

public:
    /*! Watches \a bar (and becomes its QObject child). \a groups in
     *  leading → trailing order — demotion starts at the back.
     * @param[in] bar Required borrowed toolbar, QObject parent/owner; null unsafe due
     *   installEventFilter.
     * @param[in] groups Leading-to-trailing group pointers copied as QPointers, null/deleted
     *   skipped at solve.
     * @brief Install resize/show/layout-request filter and schedule one initial zero-delay solve.
     * @note GUI-thread only; does not own groups except through their existing widget parents.
     *   Ordering determines demotion priority, last surviving group demotes first.
     */
    RibbonCompactor(QToolBar *bar, const QList<RibbonGroup *> &groups);

    /*! Solve + apply immediately (the event filter funnels here through
     *  a compressed zero-timer).
     * @note Snapshot live groups and measured widths, derive available contents width minus layout
     *   horizontal margins and one trailing spacer spacing, apply 32 pixel promotion hysteresis.
     *   Stores solved modes; each group may normalize collapsed requests. On actual mode change
     *   invalidate/set toolbar layout geometry. No groups no-op; does not cancel already queued
     *   solve, so manual call may be followed by another.
     */
    void relayoutNow();

protected:
    /**
     * @param[in] watched Borrowed watched object, toolbar pointer recognized.
     * @param[in] event Required borrowed Qt event.
     * @return QObject base-filter result; resize/show/layout events only queue solve and are never
     *   consumed here. No synchronous group mutation from filter.
     */
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    /**
     * @brief Coalesce requests into one context-bound zero-delay callback.
     * @note Pending true no-op; callback clears flag before relayoutNow, allowing later layout
     *   requests to queue again. QObject destruction cancels callback, no direct measurement now.
     */
    void scheduleRelayout();

    QToolBar *mBar = nullptr;
    QList<QPointer<RibbonGroup>> mGroups;
    QVector<RibbonMode> mModes;
    bool mPending = false;
};

} // namespace openswmmvis::ui

#endif // RIBBONCOMPACTOR_H
