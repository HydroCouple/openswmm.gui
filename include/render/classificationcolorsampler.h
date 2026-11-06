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
          m_invert(scheme.invertRamp())
    {
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
        if (!std::isfinite(value) || value < m_range.first)
            return QColor(Qt::transparent);
        const double f = std::clamp(m_range.second > m_range.first
                                        ? (value - m_range.first) / (m_range.second - m_range.first)
                                        : 0.0,
                                    0.0, 1.0);
        return m_ramp.colorAt(m_invert ? 1.0 - f : f);
    }

private:
    RasterColorRamp m_ramp;
    QPair<double, double> m_range;
    bool m_invert;
    QVector<QColor> m_classes;
};
} // namespace OpenSWMM::Render
#endif
