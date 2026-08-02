/*!
 * \file ribbongroup.cpp
 *
 * UI redesign iteration 2 (R2) — captioned ribbon group implementation.
 * See ribbongroup.h for the design contract.
 */
#include "ui/toolbars/ribbongroup.h"

#include <QAction>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QToolButton>
#include <QVBoxLayout>

#include <utility>

#include "ui/toolbars/ribbonsplitbutton.h"

namespace openswmmvis::ui {

RibbonGroup::RibbonGroup(const QString &caption, QWidget *parent)
    : QWidget(parent)
    , mCaption(caption)
{
    setFixedHeight(kRibbonRowHeight);

    auto *outer = new QHBoxLayout(this);
    outer->setContentsMargins(4, 2, 0, 2);
    outer->setSpacing(4);

    mContent = new QWidget(this);
    auto *column = new QVBoxLayout(mContent);
    column->setContentsMargins(0, 0, 0, 0);
    column->setSpacing(0);

    auto *row = new QWidget(mContent);
    mContentLayout = new QHBoxLayout(row);
    mContentLayout->setContentsMargins(0, 0, 0, 0);
    mContentLayout->setSpacing(2);
    column->addWidget(row, 1);

    mCaptionLabel = new QLabel(caption, mContent);
    mCaptionLabel->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);
    mCaptionLabel->setForegroundRole(QPalette::Mid);   // hint token
    QFont captionFont = mCaptionLabel->font();
    captionFont.setPointSizeF(captionFont.pointSizeF() * 0.85);
    mCaptionLabel->setFont(captionFont);
    column->addWidget(mCaptionLabel, 0);

    outer->addWidget(mContent, 1);

    // Collapsed face: one popup button titled by the caption whose menu
    // carries the group's actions. Hidden until setMode(Collapsed).
    mCollapsedButton = new QToolButton(this);
    mCollapsedButton->setAutoRaise(true);
    mCollapsedButton->setText(caption);
    mCollapsedButton->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    mCollapsedButton->setIconSize(QSize(kRibbonIconFull, kRibbonIconFull));
    mCollapsedButton->setPopupMode(QToolButton::InstantPopup);
    mCollapsedButton->setMenu(new QMenu(mCollapsedButton));
    mCollapsedButton->hide();
    outer->addWidget(mCollapsedButton, 1);

    mSeparator = new QFrame(this);
    mSeparator->setFrameShape(QFrame::VLine);
    mSeparator->setFrameShadow(QFrame::Plain);
    mSeparator->setForegroundRole(QPalette::Dark);     // border token
    outer->addWidget(mSeparator, 0);
}

QToolButton *RibbonGroup::makeButton()
{
    auto *button = new QToolButton(this);
    button->setAutoRaise(true);
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setIconSize(QSize(kRibbonIconFull, kRibbonIconFull));
    return button;
}

QToolButton *RibbonGroup::addAction(QAction *action, const QString &shortLabel)
{
    if (!shortLabel.isEmpty())
        action->setIconText(shortLabel);

    QToolButton *button = makeButton();
    button->setDefaultAction(action);
    mContentLayout->addWidget(button);

    mActions.append(action);
    mButtons.append(button);
    mCollapsedButton->menu()->addAction(action);
    if (mCollapsedButton->icon().isNull() && !action->icon().isNull())
        mCollapsedButton->setIcon(action->icon());

    invalidateWidthCache();
    applyMode(mMode);
    return button;
}

RibbonSplitButton *RibbonGroup::addFamily(const QString &familyId,
                                          const QList<QAction *> &members)
{
    auto *button = new RibbonSplitButton(familyId, members, this);
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setIconSize(QSize(kRibbonIconFull, kRibbonIconFull));
    mContentLayout->addWidget(button);

    mButtons.append(button);
    for (QAction *member : members) {
        mActions.append(member);
        mCollapsedButton->menu()->addAction(member);
        if (mCollapsedButton->icon().isNull() && !member->icon().isNull())
            mCollapsedButton->setIcon(member->icon());
    }

    invalidateWidthCache();
    applyMode(mMode);
    return button;
}

void RibbonGroup::addWidget(QWidget *widget, int stretch)
{
    mContentLayout->addWidget(widget, stretch);
    mCollapsible = false;
    invalidateWidthCache();
}

QToolButton *RibbonGroup::buttonForAction(const QAction *action) const
{
    for (QToolButton *button : mButtons) {
        if (button->defaultAction() == action)
            return button;
        if (auto *split = qobject_cast<RibbonSplitButton *>(button)) {
            if (split->members().contains(const_cast<QAction *>(action)))
                return button;
        }
    }
    return nullptr;
}

void RibbonGroup::setMode(RibbonMode mode)
{
    if (!mCollapsible && mode == RibbonMode::Collapsed)
        mode = RibbonMode::Compact;   // widget hosts never collapse
    if (mMode == mode)
        return;
    mMode = mode;
    applyMode(mode);
}

void RibbonGroup::applyMode(RibbonMode mode)
{
    const bool collapsed = (mode == RibbonMode::Collapsed) && mCollapsible;
    mContent->setVisible(!collapsed);
    mCollapsedButton->setVisible(collapsed);

    const bool compact = (mode == RibbonMode::Compact);
    const int edge = compact ? kRibbonIconCompact : kRibbonIconFull;
    for (QToolButton *button : std::as_const(mButtons)) {
        // Icon-less actions (transitional until every catalog row has an
        // icon) stay text-rendered so Compact never blanks them.
        const QAction *face = button->defaultAction();
        const bool hasIcon = face && !face->icon().isNull();
        Qt::ToolButtonStyle style = Qt::ToolButtonTextUnderIcon;
        if (compact)
            style = hasIcon ? Qt::ToolButtonIconOnly : Qt::ToolButtonTextOnly;
        button->setToolButtonStyle(style);
        button->setIconSize(QSize(edge, edge));
    }

    // The flips above change this group's size hint, but each nested
    // layout (button row → content column → outer row) caches its own
    // totals and only refreshes on an asynchronous LayoutRequest — and a
    // host QToolBar that has hidden an overflowing group behind its
    // extension chevron would keep judging it by the stale (wide) hint
    // and never re-show it. Invalidate the whole chain synchronously and
    // tell host layouts the hint moved.
    mContentLayout->invalidate();
    if (mContent->layout())
        mContent->layout()->invalidate();
    if (layout())
        layout()->invalidate();
    updateGeometry();
}

int RibbonGroup::widthForMode(RibbonMode m) const
{
    if (!mCollapsible)
        m = RibbonMode::Full;   // rigid: one width for every mode

    const int key = static_cast<int>(m);
    const auto it = mWidthCache.constFind(key);
    if (it != mWidthCache.cend())
        return it.value();

    // Measure from the CHILDREN's size hints, not the group's layout
    // hint: buttons refresh their own hints synchronously on style /
    // icon-size changes, while cross-widget layout caches only refresh
    // on the next LayoutRequest delivery — the group hint would serve
    // stale numbers here. mMode is left untouched, so the flip is
    // observable only through the cache being filled.
    auto *self = const_cast<RibbonGroup *>(this);
    self->applyMode(m);

    int width = 0;
    if (m == RibbonMode::Collapsed && mCollapsible) {
        width = mCollapsedButton->sizeHint().width();
    } else {
        int row = 0;
        int items = 0;
        for (int i = 0; i < mContentLayout->count(); ++i) {
            if (QWidget *w = mContentLayout->itemAt(i)->widget()) {
                row += w->sizeHint().width();
                ++items;
            }
        }
        if (items > 1)
            row += (items - 1) * mContentLayout->spacing();
        width = qMax(row, mCaptionLabel->sizeHint().width());
    }
    const QMargins margins = layout()->contentsMargins();
    width += margins.left() + margins.right() + layout()->spacing()
             + mSeparator->sizeHint().width();

    self->applyMode(mMode);

    mWidthCache.insert(key, width);
    return width;
}

QSize RibbonGroup::sizeHint() const
{
    return QSize(widthForMode(mMode), kRibbonRowHeight);
}

QSize RibbonGroup::minimumSizeHint() const
{
    // The compactor manages width through modes; below the current
    // mode's width there is nothing sensible to shrink to.
    return sizeHint();
}

RibbonGroupWidths RibbonGroup::groupWidths() const
{
    RibbonGroupWidths widths;
    widths.full        = widthForMode(RibbonMode::Full);
    widths.compact     = widthForMode(RibbonMode::Compact);
    widths.collapsed   = widthForMode(RibbonMode::Collapsed);
    widths.collapsible = mCollapsible;
    return widths;
}

void RibbonGroup::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::FontChange
        || event->type() == QEvent::StyleChange)
        invalidateWidthCache();
    QWidget::changeEvent(event);
}

}   // namespace openswmmvis::ui
