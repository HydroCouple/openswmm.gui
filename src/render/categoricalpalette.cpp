/*!
 * \file   categoricalpalette.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 */

#include "render/categoricalpalette.h"

namespace CategoricalPalette
{

namespace
{

// Tab10 — matplotlib's default qualitative palette, also widely used by
// D3 / Plotly / Seaborn.  Chosen for visual separability across hues.
const QList<QColor> &tab10()
{
    static const QList<QColor> p = {
        QColor(0x1f, 0x77, 0xb4),  // blue
        QColor(0xff, 0x7f, 0x0e),  // orange
        QColor(0x2c, 0xa0, 0x2c),  // green
        QColor(0xd6, 0x27, 0x28),  // red
        QColor(0x94, 0x67, 0xbd),  // purple
        QColor(0x8c, 0x56, 0x4b),  // brown
        QColor(0xe3, 0x77, 0xc2),  // pink
        QColor(0x7f, 0x7f, 0x7f),  // grey
        QColor(0xbc, 0xbd, 0x22),  // olive
        QColor(0x17, 0xbe, 0xcf),  // cyan
    };
    return p;
}

} // namespace

QColor at(int index)
{
    const QList<QColor> &p = tab10();
    const int n = p.size();
    int i = index % n;
    if (i < 0) i += n;
    return p.at(i);
}

int size()
{
    return tab10().size();
}

QList<QColor> palette()
{
    return tab10();
}

} // namespace CategoricalPalette
