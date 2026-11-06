#ifndef RIBBONLAYOUTSOLVER_H
#define RIBBONLAYOUTSOLVER_H

/**
 * @file      ribbonlayoutsolver.h
 * @brief     UI redesign iteration 2 (R2) — pure width solver for the captioned ribbon groups.
 *
 * UI redesign iteration 2 (R2) — pure width solver for the captioned
 * ribbon groups. Given how wide each group would be in each display
 * mode, pick per-group modes that fit the available row width by
 * demoting groups from the trailing end first (Full → Compact, then
 * Compact → Collapsed), the way ArcGIS Pro sheds detail.
 *
 * Pure functions over plain data — no widgets — so the demotion order,
 * the two-pass shape and the promotion hysteresis are unit-testable
 * headlessly.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <QVector>
#include <QtGlobal>

namespace openswmmvis::ui
{

enum class RibbonMode {
    Collapsed = 0, ///< one popup button titled by the group caption
    Compact = 1,   ///< icon-only buttons, small icons, caption kept
    Full = 2,      ///< text-under-icon buttons, large icons, caption
};

struct RibbonGroupWidths {
    int full = 0;
    int compact = 0;
    int collapsed = 0;
    bool collapsible = true; ///< false = widget-hosting group; never collapses

    /**
     * @param[in] m Mode enum; unrecognized falls back Full.
     * @return Stored width for mode, without checking nonnegativity/collapsible flag. Pure value
     *   lookup, units are logical pixels when supplied by group.
     */
    int widthFor(RibbonMode m) const
    {
        switch (m) {
        case RibbonMode::Full:
            return full;
        case RibbonMode::Compact:
            return compact;
        case RibbonMode::Collapsed:
            return collapsed;
        }
        return full;
    }
};

/*! Total row width of \a groups under \a modes, with \a spacing between
 *  adjacent groups.
 * @param[in] groups Widths in leading-to-trailing order, integer sums must not overflow.
 * @param[in] modes At least groups.size mode entries required; extra entries ignored.
 * @param[in] spacing Signed gap in same units, expected nonnegative but not clamped.
 * @return Sum group widths plus spacing between adjacent entries, 0 for empty. No validation,
 *   clamping, widget access or mutation.
 */
inline int ribbonRowWidth(const QVector<RibbonGroupWidths> &groups,
                          const QVector<RibbonMode> &modes, int spacing)
{
    int width = 0;
    for (int i = 0; i < groups.size(); ++i) {
        width += groups.at(i).widthFor(modes.at(i));
        if (i > 0)
            width += spacing;
    }
    return width;
}

/*! Demote trailing groups until the row fits \a availableWidth.
 *
 *  Pass 1 walks from the trailing end demoting Full → Compact; if the
 *  row still overflows, pass 2 walks again demoting Compact → Collapsed
 *  (collapsible groups only). A row that cannot fit even fully demoted
 *  returns the maximally-demoted assignment — the toolbar's extension
 *  chevron is the final backstop.
 * @param[in] availableWidth Signed logical-pixel budget; negative budgets still return maximally
 *   demoted assignment.
 * @param[in] groups Widths in priority order, expected nonnegative monotonic
 *   full>=compact>=collapsed, no enforcement.
 * @param[in] spacing Signed inter-group gap, expected nonnegative; total int sums must fit.
 * @return One mode per group, all Full if fit; otherwise trailing-first Full→Compact pass, then
 *   trailing-first eligible Compact→Collapsed pass, returning at first fit. If impossible returns
 *   maximally demoted, not failure/overflow flag. Pure deterministic value operation, no inputs
 *   modified.
 */
inline QVector<RibbonMode> solveRibbonModes(int availableWidth,
                                            const QVector<RibbonGroupWidths> &groups, int spacing)
{
    QVector<RibbonMode> modes(groups.size(), RibbonMode::Full);
    if (groups.isEmpty())
        return modes;
    if (ribbonRowWidth(groups, modes, spacing) <= availableWidth)
        return modes;
    for (int i = groups.size() - 1; i >= 0; --i) {
        modes[i] = RibbonMode::Compact;
        if (ribbonRowWidth(groups, modes, spacing) <= availableWidth)
            return modes;
    }
    for (int i = groups.size() - 1; i >= 0; --i) {
        if (!groups.at(i).collapsible)
            continue;
        modes[i] = RibbonMode::Collapsed;
        if (ribbonRowWidth(groups, modes, spacing) <= availableWidth)
            return modes;
    }
    return modes;
}

/*! solveRibbonModes with a promotion dead band.
 *
 *  Demotions (vs \a previous) apply immediately — shrinking must always
 *  fit. A promotion is accepted only if it would also hold with
 *  \a deadBand fewer pixels, so a window dragged across a boundary
 *  doesn't flap between modes. A \a previous of a different size (group
 *  added/removed) falls back to the plain solve.
 * @param[in] previous Prior per-group modes; size mismatch bypasses hysteresis and returns ordinary
 *   solve.
 * @param[in] availableWidth Current signed pixel budget.
 * @param[in] groups Leading-to-trailing widths/collapsibility, monotonic valid widths expected.
 * @param[in] spacing Inter-group logical-pixel gap.
 * @param[in] deadBand Promotion margin in pixels, default 32; expected nonnegative and subtraction
 *   representable, not clamped.
 * @return Candidate demotions apply immediately; promotions held at maximum of previous and
 *   solve(availableWidth-deadBand) per group. Pure value result, no mutation. Does not guarantee
 *   fit if supplied widths/modes violate normal monotonic assumptions; impossible layouts still
 *   return maximally demoted.
 */
inline QVector<RibbonMode> applyRibbonHysteresis(const QVector<RibbonMode> &previous,
                                                 int availableWidth,
                                                 const QVector<RibbonGroupWidths> &groups,
                                                 int spacing, int deadBand = 32)
{
    const QVector<RibbonMode> candidate = solveRibbonModes(availableWidth, groups, spacing);
    if (previous.size() != groups.size())
        return candidate;
    const QVector<RibbonMode> held = solveRibbonModes(availableWidth - deadBand, groups, spacing);
    QVector<RibbonMode> result = candidate;
    for (int i = 0; i < groups.size(); ++i) {
        if (candidate.at(i) > previous.at(i)) // promotion requested
            result[i] = qMax(previous.at(i), held.at(i));
    }
    return result;
}

} // namespace openswmmvis::ui

#endif // RIBBONLAYOUTSOLVER_H
