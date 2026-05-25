/*!
 * \file   intervalbinner.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 */

#include "render/intervalbinner.h"

#include <QJsonArray>

#include <algorithm>
#include <cmath>
#include <limits>

namespace OpenSWMM::Render
{

namespace
{

QVector<double> equalIntervalBreaks(double mn, double mx, int nBins)
{
    QVector<double> out;
    if (nBins <= 1) return out;
    out.reserve(nBins - 1);
    const double step = (mx - mn) / static_cast<double>(nBins);
    for (int i = 1; i < nBins; ++i)
        out.append(mn + step * static_cast<double>(i));
    return out;
}

// Fallback for empty or constant-valued samples — returns nBins-1 evenly
// spaced sentinels in {0.5, 1.5, ..., n-1.5} so binFor is still defined.
QVector<double> sentinelBreaks(int nBins)
{
    QVector<double> out;
    out.reserve(nBins - 1);
    for (int i = 1; i < nBins; ++i)
        out.append(static_cast<double>(i) - 0.5);
    return out;
}

QVector<double> quantileBreaks(QVector<double> sortedFinite, int nBins)
{
    if (sortedFinite.isEmpty() || nBins <= 1)
        return {};
    QVector<double> out;
    out.reserve(nBins - 1);
    const int n = sortedFinite.size();
    for (int i = 1; i < nBins; ++i)
    {
        // Linear-interpolated quantile: idx = i*(n-1)/nBins
        const double idxD = static_cast<double>(i) * static_cast<double>(n - 1) / static_cast<double>(nBins);
        const int   lo   = static_cast<int>(std::floor(idxD));
        const int   hi   = std::min(lo + 1, n - 1);
        const double f   = idxD - static_cast<double>(lo);
        out.append(sortedFinite[lo] + f * (sortedFinite[hi] - sortedFinite[lo]));
    }
    return out;
}

} // namespace

void IntervalBinner::setManualBreaks(QVector<double> breaks)
{
    std::sort(breaks.begin(), breaks.end());
    m_manualBreaks = std::move(breaks);
}

QVector<double> IntervalBinner::computeBreaks(const QVector<double> &samples) const
{
    if (m_method == BinMethod::Manual)
        return m_manualBreaks;

    // Collect finite values + min/max.
    QVector<double> finite;
    finite.reserve(samples.size());
    double mn =  std::numeric_limits<double>::infinity();
    double mx = -std::numeric_limits<double>::infinity();
    for (double v : samples)
    {
        if (!std::isfinite(v)) continue;
        finite.append(v);
        if (v < mn) mn = v;
        if (v > mx) mx = v;
    }

    if (finite.isEmpty() || mn == mx)
        return sentinelBreaks(m_nBins);

    switch (m_method)
    {
    case BinMethod::EqualInterval:
        return equalIntervalBreaks(mn, mx, m_nBins);
    case BinMethod::Quantile:
    {
        std::sort(finite.begin(), finite.end());
        return quantileBreaks(std::move(finite), m_nBins);
    }
    case BinMethod::Manual:
        // Already handled above; here for completeness.
        return m_manualBreaks;
    }
    return equalIntervalBreaks(mn, mx, m_nBins);
}

int IntervalBinner::binFor(double value, const QVector<double> &breaks) const
{
    if (m_nBins <= 1) return 0;
    if (!std::isfinite(value)) return 0;
    if (breaks.isEmpty())      return 0;

    // breaks is sorted ascending. The bin index is the count of break
    // values strictly less than `value`, clamped to [0, nBins-1].
    auto it = std::lower_bound(breaks.cbegin(), breaks.cend(), value);
    const int idx = static_cast<int>(std::distance(breaks.cbegin(), it));
    if (idx < 0)              return 0;
    if (idx >= m_nBins)       return m_nBins - 1;
    return idx;
}

QJsonObject IntervalBinner::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("method"), static_cast<int>(m_method));
    obj.insert(QStringLiteral("nBins"),  m_nBins);
    QJsonArray jb;
    for (double b : m_manualBreaks) jb.append(b);
    obj.insert(QStringLiteral("manualBreaks"), jb);
    return obj;
}

IntervalBinner IntervalBinner::fromJson(const QJsonObject &j)
{
    IntervalBinner b;
    const int m = j.value(QStringLiteral("method")).toInt(0);
    switch (m)
    {
    case 1: b.m_method = BinMethod::Quantile; break;
    case 2: b.m_method = BinMethod::Manual; break;
    default: b.m_method = BinMethod::EqualInterval; break;
    }
    b.m_nBins = j.value(QStringLiteral("nBins")).toInt(5);
    if (b.m_nBins < 1) b.m_nBins = 1;
    QVector<double> br;
    const QJsonArray arr = j.value(QStringLiteral("manualBreaks")).toArray();
    br.reserve(arr.size());
    for (const QJsonValue &v : arr) br.append(v.toDouble());
    std::sort(br.begin(), br.end());
    b.m_manualBreaks = br;
    return b;
}

} // namespace OpenSWMM::Render
