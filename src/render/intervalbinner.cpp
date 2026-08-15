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
#include <vector>

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

// VS.3 — Fisher–Jenks optimal natural breaks. Classic dynamic-programming
// formulation minimising the total within-class sum of squared deviations.
// Returns the (nBins-1) interior break values (global min/max excluded).
// Large inputs are stride-downsampled to keep the O(n²·k) DP bounded.
QVector<double> jenksBreaks(QVector<double> sortedFinite, int nBins)
{
    if (nBins <= 1 || sortedFinite.isEmpty())
        return {};

    // Cap the working set: the DP is O(n²·k). A few thousand sorted samples
    // capture the distribution shape; striding preserves order + endpoints.
    constexpr int kJenksCap = 1500;
    if (sortedFinite.size() > kJenksCap) {
        const int n0 = sortedFinite.size();
        QVector<double> reduced;
        reduced.reserve(kJenksCap);
        for (int i = 0; i < kJenksCap; ++i) {
            const int idx = static_cast<int>(
                static_cast<long long>(i) * (n0 - 1) / (kJenksCap - 1));
            reduced.append(sortedFinite[idx]);
        }
        sortedFinite = std::move(reduced);
    }

    const int n = sortedFinite.size();
    const int k = nBins;
    if (k >= n)  // more classes than distinct samples — fall back gracefully
        return equalIntervalBreaks(sortedFinite.front(), sortedFinite.back(), nBins);

    // 1-indexed DP matrices, dimensions (n+1) x (k+1).
    std::vector<std::vector<int>> lowerClass(
        n + 1, std::vector<int>(k + 1, 0));
    std::vector<std::vector<double>> variance(
        n + 1, std::vector<double>(k + 1, std::numeric_limits<double>::infinity()));

    for (int j = 1; j <= k; ++j) {
        lowerClass[1][j] = 1;
        variance[1][j]   = 0.0;
    }

    for (int l = 2; l <= n; ++l) {
        double s1 = 0.0, s2 = 0.0;
        int    w  = 0;
        for (int m = 1; m <= l; ++m) {
            const int    i3  = l - m + 1;            // 1-indexed lower bound
            const double val = sortedFinite[i3 - 1];
            s2 += val * val;
            s1 += val;
            ++w;
            const double var = s2 - (s1 * s1) / static_cast<double>(w);
            const int    i4  = i3 - 1;
            if (i4 != 0) {
                for (int j = 2; j <= k; ++j) {
                    if (variance[l][j] >= var + variance[i4][j - 1]) {
                        lowerClass[l][j] = i3;
                        variance[l][j]   = var + variance[i4][j - 1];
                    }
                }
            }
        }
        lowerClass[l][1] = 1;
        variance[l][1]   = s2 - (s1 * s1) / static_cast<double>(w);
    }

    // Walk the back-pointers to recover the (k-1) interior boundaries.
    QVector<double> interior;
    interior.reserve(k - 1);
    int kClass = n;
    QVector<double> bounds(k + 1, 0.0);
    for (int j = k; j >= 2; --j) {
        const int id = lowerClass[kClass][j] - 1;    // last 1-indexed elem of class j-1
        if (id >= 1 && id <= n)
            bounds[j - 1] = sortedFinite[id - 1];
        kClass = lowerClass[kClass][j] - 1;
        if (kClass < 1) kClass = 1;
    }
    for (int j = 1; j <= k - 1; ++j)
        interior.append(bounds[j]);
    return interior;
}

// VS.3 — breaks at mean ± k·σ, symmetric about the mean (one σ per band).
QVector<double> stdDevBreaks(const QVector<double> &finite, int nBins)
{
    const int n = finite.size();
    if (n == 0 || nBins <= 1) return {};
    double mean = 0.0;
    for (double v : finite) mean += v;
    mean /= static_cast<double>(n);
    double var = 0.0;
    for (double v : finite) var += (v - mean) * (v - mean);
    var /= static_cast<double>(n);
    const double sd = std::sqrt(var);
    if (sd == 0.0) return sentinelBreaks(nBins);

    QVector<double> out;
    out.reserve(nBins - 1);
    const double mid = static_cast<double>(nBins) / 2.0;
    for (int i = 1; i < nBins; ++i)
        out.append(mean + sd * (static_cast<double>(i) - mid));
    return out;
}

// VS.3 — equal spacing in log10 space. Caller guarantees mn > 0; otherwise
// returns empty so computeBreaks() can fall back to equal interval.
QVector<double> logBreaks(double mn, double mx, int nBins)
{
    if (mn <= 0.0 || nBins <= 1) return {};
    QVector<double> out;
    out.reserve(nBins - 1);
    const double lmn  = std::log10(mn);
    const double lmx  = std::log10(mx);
    const double step = (lmx - lmn) / static_cast<double>(nBins);
    for (int i = 1; i < nBins; ++i)
        out.append(std::pow(10.0, lmn + step * static_cast<double>(i)));
    return out;
}

// VS.3 — geometric (base-2) growth: each band is ~twice the previous.
QVector<double> exponentialBreaks(double mn, double mx, int nBins)
{
    if (nBins <= 1) return {};
    QVector<double> out;
    out.reserve(nBins - 1);
    const double denom = std::pow(2.0, static_cast<double>(nBins)) - 1.0;
    for (int i = 1; i < nBins; ++i) {
        const double frac = (std::pow(2.0, static_cast<double>(i)) - 1.0) / denom;
        out.append(mn + (mx - mn) * frac);
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
    case BinMethod::NaturalBreaks:
    {
        std::sort(finite.begin(), finite.end());
        return jenksBreaks(std::move(finite), m_nBins);
    }
    case BinMethod::StdDev:
        return stdDevBreaks(finite, m_nBins);
    case BinMethod::Logarithmic:
    {
        QVector<double> br = logBreaks(mn, mx, m_nBins);
        // Non-positive minimum: log is undefined — fall back to equal interval.
        return br.isEmpty() ? equalIntervalBreaks(mn, mx, m_nBins) : br;
    }
    case BinMethod::Exponential:
        return exponentialBreaks(mn, mx, m_nBins);
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
    case 3: b.m_method = BinMethod::NaturalBreaks; break;
    case 4: b.m_method = BinMethod::StdDev; break;
    case 5: b.m_method = BinMethod::Logarithmic; break;
    case 6: b.m_method = BinMethod::Exponential; break;
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
