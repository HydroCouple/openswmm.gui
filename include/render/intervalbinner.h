/*!
 * \file   intervalbinner.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BB-α — classifies a set of numeric samples into N bins.
 *
 *         Drives GraduatedRenderer's bin assignment. The renderer asks
 *         the binner to compute break values from a sample set, then per
 *         feature calls `binFor(value, breaks)` to look up which bin
 *         that feature falls into.
 *
 *         Supports EqualInterval, Quantile, Manual, NaturalBreaks (Jenks),
 *         StdDev, Logarithmic, and Exponential (VS.3). PrettyBreaks is the
 *         one remaining QGIS method, deferred because it does not yield a
 *         fixed (binCount-1) break count.
 *
 *         Cross-slice: GUI_IMPLEMENTATION_PLAN.md §L.BB-α Phase BB-α.4.
 */

#ifndef OPENSWMM_RENDER_INTERVALBINNER_H
#define OPENSWMM_RENDER_INTERVALBINNER_H

#include <QJsonObject>
#include <QVector>

namespace OpenSWMM::Render
{

/*! \enum BinMethod
 *  \brief How break values are computed from a sample set. */
enum class BinMethod : int
{
    EqualInterval = 0,  /*!< Breaks equally spaced between min and max. */
    Quantile      = 1,  /*!< Breaks such that each bin holds the same count. */
    Manual        = 2,  /*!< Breaks come from the manualBreaks field directly. */
    // VS.3 — additional standard classification methods. Each still yields
    // exactly (binCount-1) interior breaks so the GraduatedRenderer / legend
    // contract is unchanged.
    NaturalBreaks = 3,  /*!< Fisher–Jenks optimal breaks (minimise in-class variance). */
    StdDev        = 4,  /*!< Breaks at mean ± k·σ, symmetric about the mean (1σ width). */
    Logarithmic   = 5,  /*!< Equal spacing in log10 space (positive samples only). */
    Exponential   = 6,  /*!< Geometric (base-2) growth of band widths. */
};

/*! \class IntervalBinner
 *  \brief Computes break values + maps a value to a bin index. */
class IntervalBinner
{
public:
    IntervalBinner() = default;

    [[nodiscard]] BinMethod      method() const { return m_method; }
    void                         setMethod(BinMethod m) { m_method = m; }

    [[nodiscard]] int            binCount() const { return m_nBins; }
    void                         setBinCount(int n) { m_nBins = (n < 1) ? 1 : n; }

    [[nodiscard]] const QVector<double> &manualBreaks() const { return m_manualBreaks; }
    void                                 setManualBreaks(QVector<double> breaks);

    /*!
     * \brief Computes (binCount-1) interior break values from a sample of
     *        finite numeric values, per the active method.
     *
     *        - EqualInterval: linearly spaced between min and max of finite
     *          samples. If the sample is constant or empty, returns
     *          {0.5, 1.5, ..., n-1.5} so binning is well-defined.
     *        - Quantile: uses std::nth_element over a sorted copy; ties
     *          go to the lower bin.
     *        - Manual: returns manualBreaks() verbatim (callers handle
     *          mismatches between size and binCount).
     */
    [[nodiscard]] QVector<double> computeBreaks(const QVector<double> &samples) const;

    /*!
     * \brief Maps one value to its 0-based bin index, given the interior
     *        breaks previously returned by computeBreaks(). Clamps to
     *        [0, binCount-1].
     */
    [[nodiscard]] int binFor(double value, const QVector<double> &breaks) const;

    [[nodiscard]] QJsonObject toJson() const;
    static IntervalBinner     fromJson(const QJsonObject &j);

private:
    BinMethod        m_method = BinMethod::EqualInterval;
    int              m_nBins  = 5;
    QVector<double>  m_manualBreaks;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_INTERVALBINNER_H
