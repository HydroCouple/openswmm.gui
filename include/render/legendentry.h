/**
 * @file      legendentry.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     One resolved line of the map legend: a group heading, a sub-heading, a symbol row,
 *            a color-ramp bar or a line of text.
 *
 *         LegendContent::buildEntries() turns the layers' legend rows plus the legend's own
 *         settings (order, hidden rows, renamed rows) into a flat list of entries; LegendLayout
 *         places them and LegendPainter draws them. Every view of the legend (the box on the
 *         map, its text for screen readers, the copied image) reads the same list.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_LEGENDENTRY_H
#define OPENSWMM_RENDER_LEGENDENTRY_H

#include "render/symbolstyle.h"

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>

namespace OpenSWMM::Render
{

/*!
 * \struct LegendEntry
 * \brief One line of the legend, ready to lay out.
 */
struct LegendEntry {
    /** @brief What the line is. */
    enum class Kind {
        GroupHeading, ///< A layer's (or user group's) heading.
        Subheading,   ///< A row without a symbol: a section title inside a layer.
        Symbol,       ///< A symbol with its label.
        RampBar,      ///< A continuous color bar with value ticks.
        Text          ///< A line of text the user added.
    };

    Kind kind = Kind::Symbol;
    QString label;           ///< Text to draw (renames already applied).
    SymbolStyle symbol;      ///< Symbol to draw, for Kind::Symbol.
    QPointer<QObject> layer; ///< Layer the line belongs to; null for user-made lines.
    QString layerKey;        ///< Layer identity used by the legend's row settings.
    QString classKey;        ///< Renderer row key; empty when the row has none.
    QString sublayerId;      ///< Sublayer that produced the row, if any.
    int blockId = 0;         ///< Lines of one layer share a block id.
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_LEGENDENTRY_H
