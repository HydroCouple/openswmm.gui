/*!
 * \file ribboncompactor.cpp
 *
 * UI redesign iteration 2 (R2/R5) — responsive ribbon mode driver.
 * See ribboncompactor.h for the design contract.
 */
#include "ui/toolbars/ribboncompactor.h"

#include <QEvent>
#include <QLayout>
#include <QTimer>
#include <QToolBar>

#include <utility>

#include "ui/toolbars/ribbongroup.h"

namespace openswmmvis::ui {

RibbonCompactor::RibbonCompactor(QToolBar *bar, const QList<RibbonGroup *> &groups)
    : QObject(bar)
    , mBar(bar)
{
    for (RibbonGroup *group : groups)
        mGroups.append(QPointer<RibbonGroup>(group));
    bar->installEventFilter(this);
    scheduleRelayout();
}

bool RibbonCompactor::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == mBar) {
        switch (event->type()) {
        case QEvent::Resize:
        case QEvent::Show:
        case QEvent::LayoutRequest:
            scheduleRelayout();
            break;
        default:
            break;
        }
    }
    return QObject::eventFilter(watched, event);
}

void RibbonCompactor::scheduleRelayout()
{
    if (mPending)
        return;
    mPending = true;
    QTimer::singleShot(0, this, [this]() {
        mPending = false;
        relayoutNow();
    });
}

void RibbonCompactor::relayoutNow()
{
    QList<RibbonGroup *> groups;
    QVector<RibbonGroupWidths> widths;
    for (const auto &pointer : std::as_const(mGroups)) {
        if (RibbonGroup *group = pointer.data()) {
            groups.append(group);
            widths.append(group->groupWidths());
        }
    }
    if (groups.isEmpty())
        return;

    int spacing = 4;
    int margins = 0;
    if (QLayout *layout = mBar->layout()) {
        spacing = qMax(0, layout->spacing());
        const QMargins m = layout->contentsMargins();
        margins = m.left() + m.right();
    }
    int available = mBar->contentsRect().width() - margins;
    // The trailing left-pack spacer (see initializeCompactToolbar) is
    // zero-width at minimum but still costs one inter-item spacing the
    // solver doesn't know about.
    if (mBar->findChild<QWidget *>(QStringLiteral("ribbonBarSpacer"),
                                   Qt::FindDirectChildrenOnly))
        available -= spacing;

    const QVector<RibbonMode> modes =
        applyRibbonHysteresis(mModes, available, widths, spacing);
    mModes = modes;
    bool changed = false;
    for (int i = 0; i < groups.size(); ++i) {
        const RibbonMode before = groups.at(i)->mode();
        groups.at(i)->setMode(modes.at(i));
        changed = changed || groups.at(i)->mode() != before;
    }

    // A group that shrank while the toolbar had already hidden it behind
    // the extension chevron can never announce its new size on its own —
    // updateGeometry() is a no-op on hidden widgets — so the bar's
    // layout would keep judging it by the stale wide hint and never
    // re-show it. Re-run the bar's layout pass explicitly (setGeometry,
    // not activate(): the activation short-circuits leave the stale
    // arrangement in place) after mode changes.
    if (changed && mBar->layout()) {
        mBar->layout()->invalidate();
        mBar->layout()->setGeometry(mBar->contentsRect());
    }
}

}   // namespace openswmmvis::ui
