/*!
 * \file classificationcolorsampler.h
 * \brief Exact frame-local palette sampling without rebuilding ramps per cell.
 * \license GPL-3.0-or-later
 */
#ifndef OPENSWMM_RENDER_CLASSIFICATIONCOLORSAMPLER_H
#define OPENSWMM_RENDER_CLASSIFICATIONCOLORSAMPLER_H
#include "render/classificationscheme.h"
#include <algorithm>
#include <cmath>

namespace OpenSWMM::Render {
class ClassificationColorSampler
{
public:
    ClassificationColorSampler(const ClassificationScheme& scheme,
        double dataMin, double dataMax, int classes = 0)
        : m_ramp(scheme.resolvedRamp()),
          m_range(scheme.effectiveRange(dataMin, dataMax)),
          m_invert(scheme.invertRamp())
    {
        m_classes.reserve(classes);
        for (int i = 0; i < classes; ++i)
            m_classes.append(scheme.colorForClass(i, classes));
    }
    // classIndexFor returns -1 for hidden values.
    [[nodiscard]] QColor classColor(int index) const
    { return index < 0 || index >= m_classes.size() ? QColor(Qt::transparent) : m_classes.at(index); }
    [[nodiscard]] QColor valueColor(double value) const
    {
        if (!std::isfinite(value) || value < m_range.first) return QColor(Qt::transparent);
        const double f = std::clamp(m_range.second > m_range.first
            ? (value - m_range.first) / (m_range.second - m_range.first) : 0.0, 0.0, 1.0);
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
