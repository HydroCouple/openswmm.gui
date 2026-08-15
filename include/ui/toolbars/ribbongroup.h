#ifndef RIBBONGROUP_H
#define RIBBONGROUP_H

/*!
 * \file ribbongroup.h
 *
 * UI redesign iteration 2 (R2) — one captioned ribbon group: a content
 * row of action buttons (or embedded widgets) over a small centered
 * caption, closed off by a thin separator line, ArcGIS-Pro style. Lives
 * INSIDE the existing per-tab QToolBars via QToolBar::addWidget, so the
 * tabbed controller, saveState and the overflow chevron keep working.
 *
 * Three display modes (see RibbonMode): Full (text under 32 px icons),
 * Compact (icon-only 24 px, caption kept), Collapsed (everything behind
 * one popup button titled by the caption). Groups hosting member
 * widgets (combos, sliders…) report isCollapsible() == false and keep
 * one width in every mode, so the solver treats them as rigid.
 *
 * Styling is palette roles only (caption = QPalette::Mid, the hint
 * token; separator = QPalette::Dark, the border token) — theme-live
 * with zero stylesheets.
 */

#include <QHash>
#include <QList>
#include <QString>
#include <QWidget>

#include "ribbonlayoutsolver.h"

class QAction;
class QFrame;
class QHBoxLayout;
class QLabel;
class QToolButton;

namespace openswmmvis::ui {

class RibbonSplitButton;

/// Fixed height of the ribbon content + caption row (ArcGIS scale;
/// sized for a 32 px icon over TWO wrapped label lines plus the group
/// caption — see the kShortLabels '\n' wrapping in swmmvisactions.cpp).
inline constexpr int kRibbonRowHeight   = 100;
/// Icon edge in Full mode.
inline constexpr int kRibbonIconFull    = 32;
/// Icon edge in Compact mode.
inline constexpr int kRibbonIconCompact = 24;

class RibbonGroup : public QWidget
{
    Q_OBJECT

public:
    explicit RibbonGroup(const QString &caption, QWidget *parent = nullptr);

    QString caption() const { return mCaption; }

    /*! Add one action button. \a shortLabel overrides the button-face
     *  text (stored as the action's iconText — Qt's native short-label
     *  channel, so menus keep the full text) when the menu text is too
     *  long for a ribbon face. */
    QToolButton *addAction(QAction *action, const QString &shortLabel = QString());

    /*! Add a last-used split button for a family of related actions
     *  (persisted per \a familyId — see RibbonSplitButton). */
    RibbonSplitButton *addFamily(const QString &familyId,
                                 const QList<QAction *> &members);

    /*! Adopt a member widget (combo, slider…). Marks the group
     *  non-collapsible. */
    void addWidget(QWidget *widget, int stretch = 0);

    /*! The button hosting \a action (plain button or split-button face/
     *  member) — for attaching menus, e.g. the Plot-Profile dropdown. */
    QToolButton *buttonForAction(const QAction *action) const;

    void setMode(RibbonMode mode);
    RibbonMode mode() const { return mMode; }

    /*! Measured row width under \a m (cached until the group's contents
     *  or font change; widget-hosting groups report one width for every
     *  mode). */
    int widthForMode(RibbonMode m) const;

    /*! All three widths + collapsibility, ready for the solver. */
    RibbonGroupWidths groupWidths() const;

    /*! Drop the cached widths and announce the new hint — call after a
     *  hosted widget's contents change size contextually (e.g. the mesh
     *  editing clusters showing/hiding with the selection). */
    void refreshWidth()
    {
        invalidateWidthCache();
        updateGeometry();
    }

    bool isCollapsible() const { return mCollapsible; }

    /*! Actions surfaced in the collapsed popup (creation order). */
    QList<QAction *> groupActions() const { return mActions; }

    /*! Authoritative hints from the direct child measurement
     *  (widthForMode) instead of the nested-layout totals: the cache
     *  chain between the buttons and this widget breaks when a host
     *  toolbar hides the group (hidden widgets can't push
     *  updateGeometry), which would leave the host judging the group by
     *  a stale mode's width forever. */
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void changeEvent(QEvent *event) override;

private:
    void applyMode(RibbonMode mode);
    void invalidateWidthCache() { mWidthCache.clear(); }
    QToolButton *makeButton();

    QString                 mCaption;
    RibbonMode              mMode = RibbonMode::Full;
    bool                    mCollapsible = true;
    QWidget                *mContent = nullptr;
    QHBoxLayout            *mContentLayout = nullptr;
    QLabel                 *mCaptionLabel = nullptr;
    QFrame                 *mSeparator = nullptr;
    QToolButton            *mCollapsedButton = nullptr;
    QList<QAction *>        mActions;
    QList<QToolButton *>    mButtons;
    mutable QHash<int, int> mWidthCache;
};

}   // namespace openswmmvis::ui

#endif // RIBBONGROUP_H
