#ifndef RIBBONCOMPACTOR_H
#define RIBBONCOMPACTOR_H

/*!
 * \file ribboncompactor.h
 *
 * UI redesign iteration 2 (R2/R5) — watches one group-bearing QToolBar
 * and drives per-group RibbonMode from the solver whenever the bar
 * resizes or first shows: trailing groups shed detail first, and
 * promotions only land past a 32 px dead band (applyRibbonHysteresis)
 * so a window dragged across a boundary doesn't flap.
 */

#include <QList>
#include <QObject>
#include <QPointer>
#include <QVector>

#include "ribbonlayoutsolver.h"

class QToolBar;

namespace openswmmvis::ui {

class RibbonGroup;

class RibbonCompactor : public QObject
{
    Q_OBJECT

public:
    /*! Watches \a bar (and becomes its QObject child). \a groups in
     *  leading → trailing order — demotion starts at the back. */
    RibbonCompactor(QToolBar *bar, const QList<RibbonGroup *> &groups);

    /*! Solve + apply immediately (the event filter funnels here through
     *  a compressed zero-timer). */
    void relayoutNow();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void scheduleRelayout();

    QToolBar                    *mBar = nullptr;
    QList<QPointer<RibbonGroup>> mGroups;
    QVector<RibbonMode>          mModes;
    bool                         mPending = false;
};

}   // namespace openswmmvis::ui

#endif // RIBBONCOMPACTOR_H
