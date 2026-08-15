/*!
 * \file   attributesource.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  P2 — the static/dynamic styling spine.
 *
 *         A renderer classifies on an attribute. That attribute is either a
 *         STATIC engine/GIS field (diameter, elevation, land-use…), classified
 *         once, or a DYNAMIC SWMM output variable (depth, flow, velocity…)
 *         sampled at the current animation timestep. RangeMode controls how the
 *         value range behaves over the time axis:
 *           - FixedOverRun        : range spans the whole run (default; stable
 *                                   colours across frames — best for comparing
 *                                   timesteps).
 *           - PerFrameAutoStretch : range is recomputed each frame from that
 *                                   frame's values (best for seeing structure
 *                                   when magnitudes change by orders).
 *           - FixedUser           : an explicit user-set min/max.
 *
 *         These are renderer-scope styling knobs; the owning results layer
 *         consumes RangeMode on each tick to decide whether to re-bin /
 *         re-stretch the renderer, and maps the dynamic source name to its
 *         output variable.
 */
#ifndef OPENSWMM_RENDER_ATTRIBUTESOURCE_H
#define OPENSWMM_RENDER_ATTRIBUTESOURCE_H

#include <QString>

namespace OpenSWMM::Render
{

enum class AttributeSourceKind : int {
    Static  = 0,   /*!< A model/GIS attribute, classified once. */
    Dynamic = 1    /*!< A SWMM output variable, sampled per timestep. */
};

enum class RangeMode : int {
    FixedOverRun        = 0,   /*!< Range spans the whole run (default). */
    PerFrameAutoStretch = 1,   /*!< Range recomputed each frame. */
    FixedUser           = 2    /*!< Explicit user min/max. */
};

[[nodiscard]] inline QString attributeSourceKindToString(AttributeSourceKind k)
{
    return k == AttributeSourceKind::Dynamic ? QStringLiteral("dynamic")
                                             : QStringLiteral("static");
}
[[nodiscard]] inline AttributeSourceKind attributeSourceKindFromString(const QString &s)
{
    return s == QLatin1String("dynamic") ? AttributeSourceKind::Dynamic
                                         : AttributeSourceKind::Static;
}

[[nodiscard]] inline QString rangeModeToString(RangeMode m)
{
    switch (m) {
    case RangeMode::PerFrameAutoStretch: return QStringLiteral("perFrame");
    case RangeMode::FixedUser:           return QStringLiteral("fixedUser");
    case RangeMode::FixedOverRun:        break;
    }
    return QStringLiteral("fixedOverRun");
}
[[nodiscard]] inline RangeMode rangeModeFromString(const QString &s)
{
    if (s == QLatin1String("perFrame"))  return RangeMode::PerFrameAutoStretch;
    if (s == QLatin1String("fixedUser")) return RangeMode::FixedUser;
    return RangeMode::FixedOverRun;
}

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_ATTRIBUTESOURCE_H
