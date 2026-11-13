/**
 * @file      classificationcolorsampler.h
 * @brief     Exact frame-local palette sampling without rebuilding ramps per cell.
 * @license   GPL-3.0-or-later
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_CLASSIFICATIONCOLORSAMPLER_H
#define OPENSWMM_RENDER_CLASSIFICATIONCOLORSAMPLER_H
#include "render/classificationscheme.h"
#include <algorithm>
#include <cmath>

namespace OpenSWMM::Render
{
class ClassificationColorSampler
{
public:
    /**
     * @brief Capture a classification's ramp, range, inversion, and optional class colors.
     * @param[in] scheme Settings read synchronously and copied into this sampler; no
     *                   pointer or reference to the scheme is retained after construction.
     * @param[in] dataMin Automatic data-range minimum in the classified variable's unit.
     * @param[in] dataMax Automatic maximum in the same unit; custom bounds may override it.
     * @param[in] classes Non-negative number of discrete class colors to cache; zero
     *                    leaves classColor() transparent for every index.
     * @pre The scheme's range configuration is valid and is not being changed concurrently.
     * @note Later scheme edits do not update this captured sampler. Scientific values
     *       remain in their original units; this performs color mapping only.
     */
    ClassificationColorSampler(const ClassificationScheme &scheme, double dataMin, double dataMax,
                               int classes = 0)
        : m_ramp(scheme.resolvedRamp()), m_range(scheme.effectiveRange(dataMin, dataMax)),
          m_invert(scheme.invertRamp()), m_below(scheme.belowColor()), m_above(scheme.aboveColor())
    {
        // Log scale: positions run logarithmically from the scheme's log floor.
        if (scheme.logScale()) {
            const double floor = ClassificationScheme::logFloor(m_range.first, m_range.second);
            if (std::isfinite(floor) && m_range.second > floor)
                m_logFloor = floor;
        }
        m_classes.reserve(classes);
        for (int i = 0; i < classes; ++i)
            m_classes.append(scheme.colorForClass(i, classes));
    }
    /**
     * @brief Obtain a captured discrete class color without rebuilding a palette.
     * @param[in] index Zero-based class index; negative indices denote hidden values.
     * @return The cached color for an index in [0,classes), or transparent for any other
     *         index, including an index passed to a sampler created with no cached classes.
     */
    [[nodiscard]] QColor classColor(int index) const
    {
        return index < 0 || index >= m_classes.size() ? QColor(Qt::transparent)
                                                      : m_classes.at(index);
    }
    /**
     * @brief Sample the captured continuous ramp over its captured effective range.
     * @param[in] value Scalar in the same unit as the constructor's data/custom bounds.
     * @return Transparent for non-finite values or underflow; otherwise the captured
     *         ramp color at the clamped normalized position, with inversion applied.
     *         A non-increasing range uses position zero (one after inversion).
     * @note Discrete class overrides are not consulted by this continuous lookup.
     */
    [[nodiscard]] QColor valueColor(double value) const
    {
        if (!std::isfinite(value))
            return QColor(Qt::transparent);
        const double f = position(value);
        if (value < m_range.first || f < 0.0)
            return m_below.isValid() ? m_below : QColor(Qt::transparent);
        if (value > m_range.second && m_above.isValid())
            return m_above;
        return rampColor(f);
    }
    /**
     * @brief Colour of a value classified by \p edges (ClassificationScheme::levelEdges): the
     *        scheme's below / above colour outside them when set, otherwise its class colour
     *        (transparent below, the last class above). Nonfinite values are transparent.
     */
    [[nodiscard]] QColor classifiedColor(double value, const QVector<double> &edges) const
    {
        if (!std::isfinite(value))
            return QColor(Qt::transparent);
        if (edges.size() >= 2) {
            if (value < edges.front())
                return m_below.isValid() ? m_below : QColor(Qt::transparent);
            if (value > edges.back() && m_above.isValid())
                return m_above;
        }
        return classColor(ClassificationScheme::classIndexFor(value, edges));
    }
    /** @return Unclamped ramp position of \p value over the captured range: linear, or
     *  logarithmic from the log floor (values at or below zero are then below, -1). */
    [[nodiscard]] double position(double value) const
    {
        const double lo = m_range.first, hi = m_range.second;
        if (m_logFloor > 0.0)
            return value > 0.0 ? std::log(value / m_logFloor) / std::log(hi / m_logFloor) : -1.0;
        return hi > lo ? (value - lo) / (hi - lo) : 0.0;
    }
    /** @return The ramp colour at position \p f, clamped to [0, 1], inversion applied. */
    [[nodiscard]] QColor rampColor(double f) const
    {
        f = std::clamp(f, 0.0, 1.0);
        return m_ramp.colorAt(m_invert ? 1.0 - f : f);
    }
    /** @return The scheme's below-range colour (invalid: not drawn). */
    [[nodiscard]] QColor belowColor() const { return m_below; }
    /** @return The scheme's above-range colour (invalid: the top colour). */
    [[nodiscard]] QColor aboveColor() const { return m_above; }
    /** @return True when positions are logarithmic. */
    [[nodiscard]] bool logScale() const { return m_logFloor > 0.0; }
    /** @return Where the ramp starts: the log floor under a log scale, else the range minimum. */
    [[nodiscard]] double lowerBound() const
    {
        return m_logFloor > 0.0 ? std::max(m_logFloor, m_range.first) : m_range.first;
    }
    /** @return The value at ramp position \p f (inverse of position()). */
    [[nodiscard]] double valueAt(double f) const
    {
        const double lo = m_range.first, hi = m_range.second;
        if (m_logFloor > 0.0)
            return m_logFloor * std::pow(hi / m_logFloor, f);
        return lo + (hi - lo) * f;
    }

private:
    RasterColorRamp m_ramp;
    QPair<double, double> m_range;
    bool m_invert;
    QColor m_below, m_above;
    double m_logFloor = 0.0; // > 0: log scale from here
    QVector<QColor> m_classes;
};
} // namespace OpenSWMM::Render
#endif
