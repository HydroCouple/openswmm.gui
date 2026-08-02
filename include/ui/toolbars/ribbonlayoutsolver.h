#ifndef RIBBONLAYOUTSOLVER_H
#define RIBBONLAYOUTSOLVER_H

/*!
 * \file ribbonlayoutsolver.h
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
 */

#include <QVector>
#include <QtGlobal>

namespace openswmmvis::ui {

enum class RibbonMode {
    Collapsed = 0,   ///< one popup button titled by the group caption
    Compact   = 1,   ///< icon-only buttons, small icons, caption kept
    Full      = 2,   ///< text-under-icon buttons, large icons, caption
};

struct RibbonGroupWidths {
    int  full        = 0;
    int  compact     = 0;
    int  collapsed   = 0;
    bool collapsible = true;   ///< false = widget-hosting group; never collapses

    int widthFor(RibbonMode m) const
    {
        switch (m) {
        case RibbonMode::Full:      return full;
        case RibbonMode::Compact:   return compact;
        case RibbonMode::Collapsed: return collapsed;
        }
        return full;
    }
};

/*! Total row width of \a groups under \a modes, with \a spacing between
 *  adjacent groups. */
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
 *  chevron is the final backstop. */
inline QVector<RibbonMode> solveRibbonModes(int availableWidth,
                                            const QVector<RibbonGroupWidths> &groups,
                                            int spacing)
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
 *  added/removed) falls back to the plain solve. */
inline QVector<RibbonMode> applyRibbonHysteresis(const QVector<RibbonMode> &previous,
                                                 int availableWidth,
                                                 const QVector<RibbonGroupWidths> &groups,
                                                 int spacing, int deadBand = 32)
{
    const QVector<RibbonMode> candidate =
        solveRibbonModes(availableWidth, groups, spacing);
    if (previous.size() != groups.size())
        return candidate;
    const QVector<RibbonMode> held =
        solveRibbonModes(availableWidth - deadBand, groups, spacing);
    QVector<RibbonMode> result = candidate;
    for (int i = 0; i < groups.size(); ++i) {
        if (candidate.at(i) > previous.at(i))   // promotion requested
            result[i] = qMax(previous.at(i), held.at(i));
    }
    return result;
}

}   // namespace openswmmvis::ui

#endif // RIBBONLAYOUTSOLVER_H
