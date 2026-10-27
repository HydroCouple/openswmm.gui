#ifndef RIBBONGROUP_H
#define RIBBONGROUP_H

/**
 * @file      ribbongroup.h
 * @brief     UI redesign iteration 2 (R2) — one captioned ribbon group: a content row of action
 *            buttons (or embedded widgets) over a small centered caption, closed off by a thin
 *            separator line, ArcGIS-Pro style.
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
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <QHash>
#include <QList>
#include <QSet>
#include <QString>
#include <QWidget>

#include "ribbonlayoutsolver.h"

class QAction;
class QFrame;
class QHBoxLayout;
class QLabel;
class QToolButton;

namespace openswmmvis::ui
{

class RibbonSplitButton;

/// Fixed height of the ribbon content + caption row (ArcGIS scale;
/// sized for a 32 px icon over TWO wrapped label lines plus the group
/// caption — see the kShortLabels '\n' wrapping in swmmvisactions.cpp).
inline constexpr int kRibbonRowHeight = 100;
/// Icon edge in Full mode.
inline constexpr int kRibbonIconFull = 32;
/// Icon edge in Compact mode.
inline constexpr int kRibbonIconCompact = 24;

class RibbonGroup : public QWidget
{
    Q_OBJECT

public:
    /**
     * @param[in] caption Immutable copied visible caption, also sanitized to ASCII alphanumeric
     *   objectName suffix.
     * @param[in] parent Optional QWidget owner, caller owns parentless group until hosted.
     * @brief Build fixed 100 logical-pixel caption/content/separator group and hidden owned
     *   collapsed menu button.
     * @note GUI-thread only. Action pointers are borrowed, buttons/layout/menu widget children
     *   owned. Palette roles supply theme colours; no engine/model binding. Full mode initially,
     *   fixed horizontal size until positive-stretch widget added.
     */
    explicit RibbonGroup(const QString &caption, QWidget *parent = nullptr);
    /** @brief Disconnect action callbacks before QWidget destroys group-owned action children. */
    ~RibbonGroup() override;

    /**
     * @return Copied original caption, including mnemonic markers; no later setter.
     */
    QString caption() const { return mCaption; }

    /*! Add one action button. \a shortLabel overrides the button-face
     *  text (stored as the action's iconText — Qt's native short-label
     *  channel, so menus keep the full text) when the menu text is too
     *  long for a ribbon face.
     * @param[in] action Required borrowed QAction; must stay live while group/menu/buttons use it.
     * @param[in] shortLabel Nonempty overrides action iconText globally (affects other action
     *   presentations); empty retains.
     * @return Borrowed newly group-owned button bound to action.
     * @note Adds action to ordered collapsed menu/list and seeds collapsed icon from first nonnull
     *   icon; invalidates width cache and reapplies current visual mode. Does not trigger action,
     *   transfer QAction ownership or deduplicate repeated registration.
     */
    QToolButton *addAction(QAction *action, const QString &shortLabel = QString());

    /*! Add a last-used split button for a family of related actions
     *  (persisted per \a familyId — see RibbonSplitButton).
     * @param[in] familyId Settings identity passed to new split button; unique family IDs expected.
     * @param[in] members Borrowed nonnull live actions; empty list permitted but creates unbound
     *   face. Null members unsafe in group icon access.
     * @return Borrowed group-owned split button. All members are also added directly to group
     *   collapsed menu/list in supplied order.
     * @note Creates settings-restored face, invalidates widths/reapplies mode. Actions remain
     *   externally owned; toggles/triggers can later persist face selection.
     */
    RibbonSplitButton *addFamily(const QString &familyId, const QList<QAction *> &members);

    /*! Adopt a member widget (combo, slider…). Marks the group
     *  non-collapsible.
     * @param[in] widget Live widget adopted into group content layout/Qt parent ownership.
     * @param[in] stretch Layout stretch; positive makes group horizontally expanding, zero
     *   preserves fixed policy.
     * @note Permanently marks noncollapsible, clears width cache. Does not reapply current mode or
     *   explicitly updateGeometry here; caller can refreshWidth after contextual content changes.
     *   Existing collapsed mode can persist visually until later setMode/apply call. No signal.
     */
    void addWidget(QWidget *widget, int stretch = 0);

    /*! The button hosting \a action (plain button or split-button face/
     *  member) — for attaching menus, e.g. the Plot-Profile dropdown.
     * @param[in] action Borrowed identity to locate; null not specially rejected.
     * @return First matching default-action or split-family button, else null. Borrowed pointer; no
     *   ownership or trigger. Null can match unbound split face, so caller normally supplies
     *   nonnull action.
     */
    QToolButton *buttonForAction(const QAction *action) const;

    /**
     * @param[in] mode Requested Full/Compact/Collapsed mode; invalid enum not rejected.
     * @note Collapsed coerced to Compact for widget hosts or <= 1 actions. Equality after
     *   normalization no-op; otherwise stores/applies visual mode and invalidates layout hints. No
     *   modeChanged signal/settings writes.
     */
    void setMode(RibbonMode mode);
    /**
     * @return Stored normalized mode, distinct from temporary mode used during width measurement.
     */
    RibbonMode mode() const { return mMode; }

    /*! Measured row width under \a m (cached until the group's contents
     *  or font change; widget-hosting groups report one width for every
     *  mode).
     * @param[in] m Requested display mode; widget host uses Full, singleton Collapsed normalized
     *   Compact.
     * @return Cached measured logical-pixel width, including child minimum widths,
     *   captions/margins/separator/spacing.
     * @note Const API temporarily changes child visibility/button styles through applyMode to
     *   measure hints, then restores stored mode and fills mutable cache; GUI-thread only, not a
     *   pure/thread-safe getter. Hidden child widgets still counted. Width invalidated on
     *   content/font/style helpers, not every external QAction/icon/widget change; caller
     *   refreshWidth when necessary.
     */
    int widthForMode(RibbonMode m) const;

    /*! All three widths + collapsibility, ready for the solver.
     * @return Full/Compact/Collapsed measured widths and collapsible=true only for action-only
     *   group with > 1 registered actions. Getter performs temporary visual-mode measurement/cache
     *   writes; no ownership transfer.
     */
    RibbonGroupWidths groupWidths() const;

    /*! Drop the cached widths and announce the new hint — call after a
     *  hosted widget's contents change size contextually (e.g. the mesh
     *  editing clusters showing/hiding with the selection).
     * @note Clear width cache and call updateGeometry so host can resolve contextual size changes.
     *   No immediate solve, mode change or action trigger; hidden Qt widget geometry notification
     *   may need host explicit layout.
     */
    void refreshWidth()
    {
        invalidateWidthCache();
        updateGeometry();
    }

    /**
     * @return Internal no-widget flag; can be true for singleton group even though
     *   groupWidths/setMode disallow collapsed singleton. No effective-mode query.
     */
    bool isCollapsible() const { return mCollapsible; }

    /*! Actions surfaced in the collapsed popup (creation order).
     * @return Copied registration-order borrowed action list, including all family
     *   members/duplicates. Caller does not own actions through this copy; no deletion guards.
     */
    QList<QAction *> groupActions() const { return mActions; }

    /*! Authoritative hints from the direct child measurement
     *  (widthForMode) instead of the nested-layout totals: the cache
     *  chain between the buttons and this widget breaks when a host
     *  toolbar hides the group (hidden widgets can't push
     *  updateGeometry), which would leave the host judging the group by
     *  a stale mode's width forever.
     * @return Current measured mode width and 100 logical-pixel height. Calls widthForMode and can
     *   mutate child modes/caches temporarily.
     */
    QSize sizeHint() const override;
    /**
     * @return Same as sizeHint; compactor controls width through modes rather than arbitrary
     *   shrink. GUI-thread measurement side effects apply.
     */
    QSize minimumSizeHint() const override;

protected:
    /**
     * @param[in] event Required borrowed Qt event.
     * @note FontChange/StyleChange clears width cache, then QWidget base receives every event. Does
     *   not itself solve ribbon or persist style.
     */
    void changeEvent(QEvent *event) override;

private:
    /** @brief Track a hosted action's icon/text changes without taking QAction ownership.
     * @param[in] action Required borrowed command, registered once per group. */
    void watchActionPresentation(QAction *action);
    /** @brief Mirror the first available action icon and refresh measured/button presentation. */
    void refreshActionPresentation();
    /**
     * @param[in] mode Temporary/requested visual mode, does not itself assign mMode.
     * @brief Toggle content/collapsed face, set button icons/text styles and invalidate complete
     *   layout chain/updateGeometry.
     * @note Compact 32→24 logical icon size, iconless action keeps text; Full/collapsed child
     *   button size 32. No QWidget host ownership, action trigger or width-cache clear here;
     *   widthForMode uses this transiently.
     */
    void applyMode(RibbonMode mode);
    /**
     * @brief Clear measured widths only; no geometry notification, widget mutation or scheduling.
     *   Caller performs needed updateGeometry/apply/solve.
     */
    void invalidateWidthCache() { mWidthCache.clear(); }
    /**
     * @return New group-owned auto-raise text-under-icon 32 pixel button, not yet in content layout
     *   or action lists. Borrowed return; no QAction assigned.
     */
    QToolButton *makeButton();

    QString mCaption;
    RibbonMode mMode = RibbonMode::Full;
    bool mCollapsible = true;
    QWidget *mContent = nullptr;
    QHBoxLayout *mContentLayout = nullptr;
    QLabel *mCaptionLabel = nullptr;
    QFrame *mSeparator = nullptr;
    QToolButton *mCollapsedButton = nullptr;
    QList<QAction *> mActions;
    QSet<QAction *> mWatchedActions; ///< Commands with active presentation/lifetime observations.
    QList<QToolButton *> mButtons;
    mutable QHash<int, int> mWidthCache;
};

} // namespace openswmmvis::ui

#endif // RIBBONGROUP_H
