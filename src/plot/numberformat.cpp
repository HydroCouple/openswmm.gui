/*!
 * \file   numberformat.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "plot/numberformat.h"

#include <algorithm>

namespace openswmmvis::plot {

int NumberFormat::effectiveCount() const noexcept
{
    // printf 'g' treats precision 0 as 1, so floor sig figs at 1 to keep
    // the printf spec and QString::number in agreement; decimals floor at 0.
    return mode == NumberFormatMode::SignificantFigures
               ? std::max(1, count)
               : std::max(0, count);
}

char NumberFormat::fmtChar() const noexcept
{
    return mode == NumberFormatMode::SignificantFigures ? 'g' : 'f';
}

QString NumberFormat::printfSpec() const
{
    return QStringLiteral("%.") + QString::number(effectiveCount())
           + QChar::fromLatin1(fmtChar());
}

QString NumberFormat::format(double v) const
{
    return QString::number(v, fmtChar(), effectiveCount());
}

} // namespace openswmmvis::plot
