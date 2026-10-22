/**
 * @file      intervalbinner.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BB-α — classifies a set of numeric samples into N bins.
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_INTERVALBINNER_H
#define OPENSWMM_RENDER_INTERVALBINNER_H

#include <QJsonObject>
#include <QVector>

namespace OpenSWMM::Render
{

/*! \enum BinMethod
 *  \brief How break values are computed from a sample set. */
enum class BinMethod : int {
    EqualInterval = 0, /*!< Breaks equally spaced between min and max. */
    Quantile = 1,      /*!< Breaks such that each bin holds the same count. */
    Manual = 2,        /*!< Breaks come from the manualBreaks field directly. */
    // VS.3 — additional standard classification methods. Each still yields
    // exactly (binCount-1) interior breaks so the GraduatedRenderer / legend
    // contract is unchanged.
    NaturalBreaks = 3, /*!< Fisher–Jenks optimal breaks (minimise in-class variance). */
    StdDev = 4,        /*!< Breaks at mean ± k·σ, symmetric about the mean (1σ width). */
    Logarithmic = 5,   /*!< Log10 spacing for a positive range; otherwise equal intervals. */
    Exponential = 6,   /*!< Geometric (base-2) growth of band widths. */
};

/*! \class IntervalBinner
 *  \brief Computes break values + maps a value to a bin index. */
class IntervalBinner
{
public:
    /** @brief Construct equal-interval classification with five bins and no manual breaks. */
    IntervalBinner() = default;

    /**
     * @brief Query the active classification method without computing breaks.
     * @return The configured classification method.
     */
    [[nodiscard]] BinMethod method() const { return m_method; }

    /**
     * @brief Replace the classification method without recomputing any breaks.
     * @param[in] m New method; an unrecognized value later falls back to equal intervals.
     */
    void setMethod(BinMethod m) { m_method = m; }

    /**
     * @brief Query the requested number of classification bins.
     * @return The configured bin count, always at least one.
     */
    [[nodiscard]] int binCount() const { return m_nBins; }

    /**
     * @brief Set the classification bin count, clamping only its lower bound.
     * @param[in] n Requested bin count; values below one become one. No upper cap is applied.
     * @note Existing manual breaks are neither resized nor recomputed by this setter.
     */
    void setBinCount(int n) { m_nBins = (n < 1) ? 1 : n; }

    /**
     * @brief Borrow the current sorted manual-break sequence.
     * @return Const reference owned by this binner; do not retain it across replacement
     *         of the breaks or destruction of the binner.
     */
    [[nodiscard]] const QVector<double> &manualBreaks() const { return m_manualBreaks; }

    /**
     * @brief Sort and take ownership of a caller-supplied manual break sequence.
     * @param[in] breaks Breaks in the sample attribute's unit; passed by value and sorted
     *                   ascending before replacing the stored vector. Duplicates are retained.
     * @pre Breaks must be finite for a valid numeric ordering.
     * @note The method, bin count, and number of manual breaks are not synchronized here.
     */
    void setManualBreaks(QVector<double> breaks);

    /*!
     * \brief Computes (binCount-1) interior break values from a sample of
     *        finite numeric values, per the active method.
     *
     *        - EqualInterval: linearly spaced between min and max of finite
     *          samples. If the sample is constant or empty, returns
     *          {0.5, 1.5, ..., n-1.5} so binning is well-defined.
     *        - Quantile: sorts a finite copy and linearly interpolates the
     *          i*(sampleCount-1)/binCount quantile positions.
     *        - Manual: returns manualBreaks() verbatim (callers handle
     *          mismatches between size and binCount).
     * @param[in] samples Attribute values in one consistent unit. Except in Manual mode,
     *                    NaN and infinite samples are ignored and the input is not modified.
     * @return An owned sequence of binCount()-1 interior breaks for computed methods, or
     *         the stored manual breaks regardless of their count. Empty/constant finite
     *         samples produce the documented sentinel sequence rather than data-scaled cuts.
     * @details NaturalBreaks applies Fisher-Jenks dynamic programming, with sorted inputs
     *          reduced deterministically to at most 1,500 samples. If the bin count is
     *          at least that working sample count, it falls back to equal intervals.
     *          StdDev uses population variance; Logarithmic falls back to linear spacing
     *          when the minimum is non-positive; Exponential uses base-two band growth.
     * @note This query does not update stored configuration. NaturalBreaks has quadratic
     *       cost in its capped sample count and linear cost in the requested bin count.
     */
    [[nodiscard]] QVector<double> computeBreaks(const QVector<double> &samples) const;

    /*!
     * \brief Maps one value to its 0-based bin index, given the interior
     *        breaks previously returned by computeBreaks(). Clamps to
     *        [0, binCount-1].
     * @param[in] value Attribute value in the same unit as breaks.
     * @param[in] breaks Ascending finite interior break values; normally from computeBreaks().
     * @return Count of breaks strictly less than value, clamped to [0,binCount()-1].
     *         Values equal to a break stay in its lower bin. Non-finite values, an empty
     *         break sequence, or a single-bin configuration return zero.
     * @pre The supplied breaks must be sorted; this function does not sort or validate them.
     */
    [[nodiscard]] int binFor(double value, const QVector<double> &breaks) const;

    /**
     * @brief Serialize method, bin count, and manual breaks for a project style.
     * @return Owned JSON object using integer method identifiers and a numeric break array.
     *         Stored defaults are included; this performs no recomputation or validation.
     */
    [[nodiscard]] QJsonObject toJson() const;

    /**
     * @brief Construct classification settings from persisted JSON.
     * @param[in] j Object containing method, nBins, and manualBreaks; unknown keys are ignored.
     * @return New binner with the recognized method, at least one bin, and sorted manual
     *         breaks. Missing/wrong-typed fields default to equal intervals, five bins,
     *         and no breaks. Unrecognized method IDs use equal intervals; non-numeric
     *         entries in the break array convert to zero through QJsonValue::toDouble().
     * @note No agreement between manual break count and bin count is enforced.
     */
    static IntervalBinner fromJson(const QJsonObject &j);

private:
    BinMethod m_method = BinMethod::EqualInterval;
    int m_nBins = 5;
    QVector<double> m_manualBreaks;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_INTERVALBINNER_H
