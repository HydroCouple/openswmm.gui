/*!
 * \file   numberformat.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */
#include "plot/numberformat.h"

#include <QLocale>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>
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
    switch (mode) {
    case NumberFormatMode::SignificantFigures: return 'g';
    case NumberFormatMode::Scientific:
    case NumberFormatMode::Engineering:        return 'e';
    case NumberFormatMode::Decimals:
    case NumberFormatMode::Thousands:
    default:                                   return 'f';
    }
}

namespace {

// 12.3e+03 style: exponent snapped down to a multiple of 3, mantissa in
// [1, 1000) with `decimals` places, exponent always signed and 2+ digits
// like printf 'e' so the two scientific styles read alike.
QString formatEngineering(double v, int decimals)
{
    if (!std::isfinite(v)) return QString::number(v);
    if (v == 0.0)
        return QStringLiteral("%1e+00").arg(QString::number(0.0, 'f', decimals));
    int e3 = static_cast<int>(std::floor(std::log10(std::fabs(v)) / 3.0)) * 3;
    double mant = v / std::pow(10.0, e3);
    // Rounding at `decimals` can carry the mantissa to 1000 — renormalise.
    const double lim = 1000.0 - 0.5 * std::pow(10.0, -decimals);
    if (std::fabs(mant) >= lim) { mant /= 1000.0; e3 += 3; }
    const QString exp = QStringLiteral("%1%2")
                            .arg(e3 < 0 ? QLatin1Char('-') : QLatin1Char('+'))
                            .arg(std::abs(e3), 2, 10, QLatin1Char('0'));
    return QString::number(mant, 'f', decimals) + QLatin1Char('e') + exp;
}

} // namespace

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
    switch (mode) {
    case NumberFormatMode::Engineering:
        return formatEngineering(v, effectiveCount());
    case NumberFormatMode::Thousands:
        if (!std::isfinite(v)) return QString::number(v);
        return QLocale().toString(v, 'f', effectiveCount());   // grouped
    default:
        return QString::number(v, fmtChar(), effectiveCount());
    }
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
    { NumberFormatMode::Scientific,         2 },   // Scientific2
    { NumberFormatMode::Scientific,         3 },   // Scientific3
    { NumberFormatMode::Scientific,         4 },   // Scientific4
    { NumberFormatMode::Engineering,        2 },   // Engineering2
    { NumberFormatMode::Engineering,        3 },   // Engineering3
    { NumberFormatMode::Thousands,          0 },   // ThousandsInteger
    { NumberFormatMode::Thousands,          1 },   // Thousands1
    { NumberFormatMode::Thousands,          2 },   // Thousands2
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
