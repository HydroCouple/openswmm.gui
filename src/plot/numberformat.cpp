/*!
 * \file   numberformat.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "plot/numberformat.h"

#include <QRegularExpression>

#include <algorithm>
#include <cstdlib>

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

// ---------------------------------------------------------------------------
// Combined presets
// ---------------------------------------------------------------------------

namespace {

struct PresetDef { NumberFormatMode mode; int count; };

// Index == AxisNumberFormatPreset value. Order is persisted; append only.
constexpr PresetDef kPresets[axisNumberFormatPresetCount] = {
    { NumberFormatMode::Decimals,           0 },   // Integer
    { NumberFormatMode::Decimals,           1 },   // Decimals1
    { NumberFormatMode::Decimals,           2 },   // Decimals2
    { NumberFormatMode::Decimals,           3 },   // Decimals3
    { NumberFormatMode::Decimals,           4 },   // Decimals4
    { NumberFormatMode::Decimals,           6 },   // Decimals6
    { NumberFormatMode::SignificantFigures, 3 },   // SigFigs3
    { NumberFormatMode::SignificantFigures, 4 },   // SigFigs4
    { NumberFormatMode::SignificantFigures, 6 },   // SigFigs6
};

} // namespace

NumberFormat numberFormatForPreset(int preset)
{
    NumberFormat f;
    if (preset < 0 || preset >= axisNumberFormatPresetCount) {
        f.mode  = NumberFormatMode::Decimals;
        f.count = 2;                       // historic default
        return f;
    }
    f.mode  = kPresets[preset].mode;
    f.count = kPresets[preset].count;
    return f;
}

int presetForNumberFormat(NumberFormatMode mode, int count)
{
    // Exact match first, then the nearest count within the same mode, so a
    // stored "5 decimals" migrates to Decimals6 instead of collapsing to the
    // default. Only if the mode has no presets at all do we fall back.
    //
    // Ties go to the LATER preset (`<=`, and the table ascends by count):
    // 5 decimals sits between Decimals4 and Decimals6, and showing the user
    // fewer digits than they asked for is the worse of the two errors.
    int best = -1, bestDist = 0;
    for (int i = 0; i < axisNumberFormatPresetCount; ++i) {
        if (kPresets[i].mode != mode) continue;
        const int dist = std::abs(kPresets[i].count - count);
        if (dist == 0) return i;
        if (best < 0 || dist <= bestDist) { best = i; bestDist = dist; }
    }
    return best >= 0 ? best : static_cast<int>(Decimals2);
}

} // namespace openswmmvis::plot
