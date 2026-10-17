/*!
 * \file   sizechannel.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Renderer-independent size channel for results sublayers.
 *
 *         Drives a kind's node marker size (by area) or link stroke width
 *         (linearly) from a result variable, independently of whichever
 *         renderer colours the kind. Owned by the kind's sublayer style
 *         (PointFeatureSublayerStyle / LineFeatureSublayerStyle) and sampled
 *         per frame by SWMMResultsLayer::rebuildKindFeatureOverrides.
 *
 *         The math mirrors the trace layer's size scale
 *         (traceanalysislayer.cpp: transform / position / symbolSize).
 *
 *         Header-only so the header-only sublayer styles keep carrying no
 *         link dependency.
 */
#ifndef OPENSWMM_RENDER_SIZECHANNEL_H
#define OPENSWMM_RENDER_SIZECHANNEL_H

#include "render/attributesource.h"

#include <QJsonObject>
#include <QString>

#include <algorithm>
#include <cmath>
#include <limits>

namespace OpenSWMM::Render
{

struct SizeChannel
{
    enum class Transform { Linear = 0, Sqrt, Log };

    bool      enabled    = false;
    QString   attribute;                          ///< result variable (classifyAttribute namespace)
    Transform transform  = Transform::Linear;
    RangeMode rangeMode  = RangeMode::FixedOverRun;
    double    userMin    = 0.0;                   ///< FixedUser only
    double    userMax    = 1.0;
    bool      zeroBased  = true;                  ///< range starts at 0 (trace "proportional")
    double    minPx      = 2.0;
    double    maxPx      = 14.0;
    bool      areaScaled = false;                 ///< true: interpolate marker area (nodes)

    [[nodiscard]] static double transformed(double v, Transform t)
    {
        switch (t) {
        case Transform::Sqrt: return std::sqrt(std::max(0.0, v));
        case Transform::Log:  return std::log1p(std::max(0.0, v));
        case Transform::Linear: break;
        }
        return v;
    }

    /*! Lower bound actually used: 0 when zeroBased and the range is positive. */
    [[nodiscard]] double effectiveLow(double lo) const
    {
        return zeroBased ? std::min(0.0, lo) : lo;
    }

    /*! 0..1 position of \p v in [lo, hi] after the transform; NaN when \p v
     *  is not finite, 0.5 for a degenerate range. */
    [[nodiscard]] double position(double v, double lo, double hi) const
    {
        if (!std::isfinite(v))
            return std::numeric_limits<double>::quiet_NaN();
        const double a = transformed(effectiveLow(lo), transform);
        const double b = transformed(hi, transform);
        return b > a ? std::clamp((transformed(v, transform) - a) / (b - a), 0.0, 1.0) : 0.5;
    }

    /*! Pixel size for \p v; NaN when \p v is not finite (caller: no override). */
    [[nodiscard]] double pixels(double v, double lo, double hi) const
    {
        const double t = position(v, lo, hi);
        if (!std::isfinite(t))
            return std::numeric_limits<double>::quiet_NaN();
        return areaScaled ? std::sqrt(minPx * minPx + t * (maxPx * maxPx - minPx * minPx))
                          : minPx + t * (maxPx - minPx);
    }

    [[nodiscard]] static QString transformToString(Transform t)
    {
        switch (t) {
        case Transform::Sqrt: return QStringLiteral("sqrt");
        case Transform::Log:  return QStringLiteral("log");
        case Transform::Linear: break;
        }
        return QStringLiteral("linear");
    }
    [[nodiscard]] static Transform transformFromString(const QString &s)
    {
        if (s == QLatin1String("sqrt")) return Transform::Sqrt;
        if (s == QLatin1String("log"))  return Transform::Log;
        return Transform::Linear;
    }

    [[nodiscard]] QJsonObject toJson() const
    {
        QJsonObject j;
        j[QStringLiteral("enabled")]   = enabled;
        j[QStringLiteral("attribute")] = attribute;
        j[QStringLiteral("transform")] = transformToString(transform);
        j[QStringLiteral("rangeMode")] = rangeModeToString(rangeMode);
        j[QStringLiteral("userMin")]   = userMin;
        j[QStringLiteral("userMax")]   = userMax;
        j[QStringLiteral("zeroBased")] = zeroBased;
        j[QStringLiteral("minPx")]     = minPx;
        j[QStringLiteral("maxPx")]     = maxPx;
        return j;
    }

    /*! \p areaScaledForKind is the archetype's value, not persisted. */
    [[nodiscard]] static SizeChannel fromJson(const QJsonObject &j, bool areaScaledForKind)
    {
        SizeChannel c;
        c.areaScaled = areaScaledForKind;
        c.enabled    = j.value(QStringLiteral("enabled")).toBool(false);
        c.attribute  = j.value(QStringLiteral("attribute")).toString();
        c.transform  = transformFromString(j.value(QStringLiteral("transform")).toString());
        c.rangeMode  = rangeModeFromString(j.value(QStringLiteral("rangeMode")).toString());
        c.userMin    = j.value(QStringLiteral("userMin")).toDouble(0.0);
        c.userMax    = j.value(QStringLiteral("userMax")).toDouble(1.0);
        c.zeroBased  = j.value(QStringLiteral("zeroBased")).toBool(true);
        c.minPx      = j.value(QStringLiteral("minPx")).toDouble(2.0);
        c.maxPx      = j.value(QStringLiteral("maxPx")).toDouble(14.0);
        return c;
    }

    friend bool operator==(const SizeChannel &a, const SizeChannel &b)
    {
        return a.enabled == b.enabled && a.attribute == b.attribute
            && a.transform == b.transform && a.rangeMode == b.rangeMode
            && a.userMin == b.userMin && a.userMax == b.userMax
            && a.zeroBased == b.zeroBased && a.minPx == b.minPx
            && a.maxPx == b.maxPx && a.areaScaled == b.areaScaled;
    }
    friend bool operator!=(const SizeChannel &a, const SizeChannel &b) { return !(a == b); }
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_SIZECHANNEL_H
