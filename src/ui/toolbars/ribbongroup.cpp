/**
 * @file      ribbongroup.cpp
 * @brief     UI redesign iteration 2 (R2) — one captioned ribbon group: a content row of action
 *            buttons (or embedded widgets) over a small centered caption, closed off by a thin
 *            separator line, ArcGIS-Pro style.
 *
 * UI redesign iteration 2 (R2) — captioned ribbon group implementation.
 * See ribbongroup.h for the design contract.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
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
#include <QRegularExpression>

namespace openswmmvis::ui
{

namespace
{

/*! One stacked row. The style's tool-button margins (about 10 px a side on
 *  macOS) suit a lone button; rows packed into columns only need a few
 *  pixels, and the saving is what keeps labels visible at common window
 *  widths. Contents are laid out exactly as QToolButton paints them: icon,
 *  4 px, then the label. */
class StackButton final : public QToolButton
{
public:
    explicit StackButton(QWidget *parent) : QToolButton(parent) {}

    QSize sizeHint() const override
    {
        const QSize base = QToolButton::sizeHint();
        if (toolButtonStyle() == Qt::ToolButtonIconOnly || text().isEmpty())
            return base;
        constexpr int kPad = 4;
        const int label = fontMetrics().horizontalAdvance(text());
        const int icon =
            toolButtonStyle() == Qt::ToolButtonTextOnly ? 0 : iconSize().width() + 4;
        return QSize(qMin(base.width(), icon + label + 2 * kPad), base.height());
    }
    QSize minimumSizeHint() const override { return sizeHint(); }
};

} // namespace

RibbonGroup::RibbonGroup(const QString &caption, QWidget *parent)
    : QWidget(parent), mCaption(caption)
{
    // Name the group after its caption ("Display" -> "ribbonGroupDisplay").
    // Every group is otherwise anonymous and indistinguishable by class, so
    // nothing can address one: the user manual's figures need to grab the
    // Display and Performance groups specifically.
    QString slug = caption;
    slug.remove(QLatin1Char('&'));
    slug.remove(QRegularExpression(QStringLiteral("[^A-Za-z0-9]")));
    if (!slug.isEmpty())
        setObjectName(QStringLiteral("ribbonGroup") + slug);

    setFixedHeight(kRibbonRowHeight);
    // Fixed horizontal policy: the host QToolBar's layout hands leftover
    // row width equally to every item that can grow, which inflated all
    // groups to a uniform pitch and drifted the icons apart. A group is
    // exactly as wide as its measured mode width — leftover space
    // collects in the bar's trailing spacer instead. addWidget() flips
    // this to Expanding when a stretch child (the timeline slider)
    // should absorb the slack.
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

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
    mCaptionLabel->setForegroundRole(QPalette::Mid); // hint token
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
    mSeparator->setForegroundRole(QPalette::Dark); // border token
    outer->addWidget(mSeparator, 0);
}

RibbonGroup::~RibbonGroup()
{
    // QObject child destruction follows destruction of the derived member containers.
    // Actions parented to this group must not call those containers during teardown.
    for (auto *action : std::as_const(mWatchedActions))
        disconnect(action, nullptr, this, nullptr);
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
    watchActionPresentation(action);
    mButtons.append(button);
    mCollapsedButton->menu()->addAction(action);
    if (mCollapsedButton->icon().isNull() && !action->icon().isNull())
        mCollapsedButton->setIcon(action->icon());

    invalidateWidthCache();
    applyMode(mMode);
    return button;
}

RibbonSplitButton *RibbonGroup::addFamily(const QString &familyId, const QList<QAction *> &members)
{
    auto *button = new RibbonSplitButton(familyId, members, this);
    button->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    button->setIconSize(QSize(kRibbonIconFull, kRibbonIconFull));
    mContentLayout->addWidget(button);

    mButtons.append(button);
    for (QAction *member : members) {
        mActions.append(member);
        watchActionPresentation(member);
        mCollapsedButton->menu()->addAction(member);
        if (mCollapsedButton->icon().isNull() && !member->icon().isNull())
            mCollapsedButton->setIcon(member->icon());
    }

    invalidateWidthCache();
    applyMode(mMode);
    return button;
}

QList<QToolButton *> RibbonGroup::addStack(const QList<QAction *> &actions)
{
    auto *column = new QWidget(this);
    column->setObjectName(QStringLiteral("ribbonStackColumn"));
    auto *rows = new QVBoxLayout(column);
    rows->setContentsMargins(0, 0, 0, 0);
    rows->setSpacing(1);

    QList<QToolButton *> buttons;
    for (QAction *action : actions.mid(0, kRibbonStackRows)) {
        auto *button = new StackButton(column);
        button->setAutoRaise(true);
        button->setDefaultAction(action);
        button->setFixedHeight(kRibbonStackRowHeight);
        button->setIconSize(QSize(kRibbonIconStack, kRibbonIconStack));
        rows->addWidget(button, 0, Qt::AlignLeft);

        mActions.append(action);
        watchActionPresentation(action);
        mButtons.append(button);
        mStackButtons.insert(button);
        mCollapsedButton->menu()->addAction(action);
        if (mCollapsedButton->icon().isNull() && !action->icon().isNull())
            mCollapsedButton->setIcon(action->icon());
        buttons.append(button);
    }
    rows->addStretch(1);
    mContentLayout->addWidget(column, 0, Qt::AlignTop);
    mStackColumns.insert(column, buttons);

    invalidateWidthCache();
    applyMode(mMode);
    return buttons;
}

void RibbonGroup::watchActionPresentation(QAction *action)
{
    if (mWatchedActions.contains(action))
        return;
    mWatchedActions.insert(action);
    connect(action, &QAction::changed, this, &RibbonGroup::refreshActionPresentation);
    connect(action, &QObject::destroyed, this, [this, action] {
        mWatchedActions.remove(action);
        mActions.removeAll(action);
        refreshActionPresentation();
    });
}

void RibbonGroup::refreshActionPresentation()
{
    QIcon icon;
    for (auto *action : std::as_const(mActions))
        if (!action->icon().isNull()) {
            icon = action->icon();
            break;
        }
    mCollapsedButton->setIcon(icon);
    invalidateWidthCache();
    setMode(mMode);
    applyMode(mMode);
}

void RibbonGroup::addWidget(QWidget *widget, int stretch)
{
    mContentLayout->addWidget(widget, stretch);
    mCollapsible = false;
    if (stretch > 0)
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
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
    if ((!mCollapsible || mActions.size() <= 1) && mode == RibbonMode::Collapsed)
        mode = RibbonMode::Compact; // widget hosts and single tools
                                    // never become a caption dropdown
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
        if (mStackButtons.contains(button)) {
            // Stacked rows keep their small icon in every mode; Compact
            // only drops the label beside it.
            Qt::ToolButtonStyle rowStyle =
                hasIcon ? Qt::ToolButtonTextBesideIcon : Qt::ToolButtonTextOnly;
            if (compact && hasIcon)
                rowStyle = Qt::ToolButtonIconOnly;
            button->setToolButtonStyle(rowStyle);
            continue;
        }
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
    for (auto it = mStackColumns.cbegin(); it != mStackColumns.cend(); ++it)
        if (it.key()->layout())
            it.key()->layout()->invalidate();
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
        m = RibbonMode::Full; // rigid: one width for every mode
    else if (mActions.size() <= 1 && m == RibbonMode::Collapsed)
        m = RibbonMode::Compact; // single tool: no dropdown face

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
                const auto stack = mStackColumns.constFind(w);
                if (stack != mStackColumns.cend()) {
                    // A column is as wide as its widest row. Read the rows
                    // directly: the column's own layout hint is cached and
                    // lags the style flip applied just above.
                    int widest = 0;
                    for (QToolButton *button : stack.value())
                        if (!button->isHidden())
                            widest = qMax(widest, button->sizeHint().width());
                    row += widest;
                    ++items;
                    continue;
                }
                // A raw sizeHint ignores an explicit setMinimumWidth
                // (combos, the timeline slider), while the inner layout
                // clamps the widget back up to it — under-reporting here
                // once made the analysis combos overlap when starved.
                row += qMax(w->sizeHint().width(), w->minimumWidth());
                ++items;
            }
        }
        if (items > 1)
            row += (items - 1) * mContentLayout->spacing();
        width = qMax(row, mCaptionLabel->sizeHint().width());
    }
    const QMargins margins = layout()->contentsMargins();
    width +=
        margins.left() + margins.right() + layout()->spacing() + mSeparator->sizeHint().width();

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
    widths.full = widthForMode(RibbonMode::Full);
    widths.compact = widthForMode(RibbonMode::Compact);
    widths.collapsed = widthForMode(RibbonMode::Collapsed);
    // A single-action group reports non-collapsible so the solver never
    // demotes one lone tool into a caption-titled dropdown; its Compact
    // (icon-only) mode is the floor.
    widths.collapsible = mCollapsible && mActions.size() > 1;
    return widths;
}

void RibbonGroup::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::FontChange || event->type() == QEvent::StyleChange)
        invalidateWidthCache();
    QWidget::changeEvent(event);
}

} // namespace openswmmvis::ui
