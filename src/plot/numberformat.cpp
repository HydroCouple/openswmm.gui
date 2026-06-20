/*!
 * \file   numberformat.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "plot/numberformat.h"

#include <QRegularExpression>

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

bool NumberFormat::hasValidCustom() const
{
    if (custom.isEmpty()) return false;
    // Exactly one floating-point conversion (optionally surrounded by literal
    // text and escaped "%%"). Reject '*' width/precision and any non-float
    // specifier so QString::asprintf(custom, double) is type-safe.
    static const QRegularExpression re(QStringLiteral(
        "\\A(?:[^%]|%%)*%[-+ #0]*[0-9]*(?:\\.[0-9]+)?[eEfFgGaA](?:[^%]|%%)*\\z"));
    return re.match(custom).hasMatch();
}

QString NumberFormat::printfSpec() const
{
    if (hasValidCustom()) return custom;
    return QStringLiteral("%.") + QString::number(effectiveCount())
           + QChar::fromLatin1(fmtChar());
}

QString NumberFormat::format(double v) const
{
    if (hasValidCustom())
        return QString::asprintf(custom.toUtf8().constData(), v);
    return QString::number(v, fmtChar(), effectiveCount());
}

} // namespace openswmmvis::plot
