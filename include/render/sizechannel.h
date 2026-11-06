/**
 * @file      sizechannel.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Renderer-independent size channel for results sublayers.
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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

struct SizeChannel {
    enum class Transform { Linear = 0, Sqrt, Log };

    bool enabled = false;
    QString attribute; ///< result variable (classifyAttribute namespace)
    Transform transform = Transform::Linear;
    RangeMode rangeMode = RangeMode::FixedOverRun;
    double userMin = 0.0; ///< FixedUser only
    double userMax = 1.0;
    bool zeroBased = true; ///< range starts at 0 (trace "proportional")
    double minPx = 2.0;
    double maxPx = 14.0;
    bool areaScaled = false; ///< true: interpolate marker area (nodes)

    /**
     * @brief Map a scalar into the size channel's interpolation domain.
     * @param[in] v Scalar or range endpoint in the source attribute's native unit.
     * @param[in] t Linear, square-root, or natural-log-plus-one transform to apply.
     * @return v for Linear or an unrecognized transform; sqrt(max(0,v)) for Sqrt;
     *         log1p(max(0,v)) for Log. The nonlinear forms clip negative inputs to zero.
     * @note This helper itself does not reject non-finite values. position() validates
     *       the sampled scalar before calling it; callers should supply finite endpoints.
     */
    [[nodiscard]] static double transformed(double v, Transform t)
    {
        switch (t) {
        case Transform::Sqrt:
            return std::sqrt(std::max(0.0, v));
        case Transform::Log:
            return std::log1p(std::max(0.0, v));
        case Transform::Linear:
            break;
        }
        return v;
    }

    /**
     * @brief Include zero in a positive lower range when proportional sizing is enabled.
     * @param[in] lo Original lower endpoint in the source attribute's unit.
     * @return min(0,lo) when zeroBased is true; otherwise lo unchanged. A negative
     *         lower endpoint is preserved even in zero-based mode.
     */
    [[nodiscard]] double effectiveLow(double lo) const
    {
        return zeroBased ? std::min(0.0, lo) : lo;
    }

    /**
     * @brief Normalize a sample in the selected transformed sizing range.
     * @param[in] v Source value, in the same unit as lo and hi.
     * @param[in] lo Finite lower endpoint before zeroBased adjustment and transformation.
     * @param[in] hi Finite upper endpoint before transformation.
     * @return A position in [0,1] for an increasing transformed range; 0.5 when that
     *         range is degenerate or reversed; quiet NaN when v is not finite.
     * @details Applies effectiveLow() first, then the configured transform to sample
     *          and endpoints. Samples outside a valid range saturate to an endpoint.
     *          No enabled/attribute/rangeMode check or field-range discovery occurs here.
     */
    [[nodiscard]] double position(double v, double lo, double hi) const
    {
        if (!std::isfinite(v))
            return std::numeric_limits<double>::quiet_NaN();
        const double a = transformed(effectiveLow(lo), transform);
        const double b = transformed(hi, transform);
        return b > a ? std::clamp((transformed(v, transform) - a) / (b - a), 0.0, 1.0) : 0.5;
    }

    /**
     * @brief Convert an attribute sample into a screen-pixel marker size or line width.
     * @param[in] v Source sample in its native attribute unit; non-finite means no override.
     * @param[in] lo Finite source-range lower endpoint, before zeroBased adjustment.
     * @param[in] hi Finite source-range upper endpoint.
     * @return A size interpolated between minPx and maxPx, or quiet NaN for a non-finite
     *         normalized sample. Both minPx and maxPx are screen-pixel sizes.
     * @details Linear sizing interpolates size directly. With areaScaled, squared sizes
     *          are interpolated and square-rooted so a point marker's area follows the
     *          normalized attribute. This method does not validate sizing configuration
     *          or check enabled; callers decide whether to apply the returned override.
     */
    [[nodiscard]] double pixels(double v, double lo, double hi) const
    {
        const double t = position(v, lo, hi);
        if (!std::isfinite(t))
            return std::numeric_limits<double>::quiet_NaN();
        return areaScaled ? std::sqrt(minPx * minPx + t * (maxPx * maxPx - minPx * minPx))
                          : minPx + t * (maxPx - minPx);
    }

    /**
     * @brief Encode the size transform in its persisted spelling.
     * @param[in] t Transform to encode.
     * @return "sqrt", "log", or "linear"; unrecognized enum values serialize as linear.
     */
    [[nodiscard]] static QString transformToString(Transform t)
    {
        switch (t) {
        case Transform::Sqrt:
            return QStringLiteral("sqrt");
        case Transform::Log:
            return QStringLiteral("log");
        case Transform::Linear:
            break;
        }
        return QStringLiteral("linear");
    }
    /**
     * @brief Decode a case-sensitive size-transform token.
     * @param[in] s Persisted token, without trimming or case normalization.
     * @return Sqrt for "sqrt", Log for "log", and Linear for all other input.
     */
    [[nodiscard]] static Transform transformFromString(const QString &s)
    {
        if (s == QLatin1String("sqrt"))
            return Transform::Sqrt;
        if (s == QLatin1String("log"))
            return Transform::Log;
        return Transform::Linear;
    }

    /**
     * @brief Serialize user-configurable channel settings into a project JSON object.
     * @return Owned JSON values for enabled, attribute, transform, rangeMode, userMin,
     *         userMax, zeroBased, minPx, and maxPx; defaults are included explicitly.
     * @note areaScaled is derived from the owning geometry archetype and is not persisted.
     *       Serialization performs no validation of ranges, units, or finite values.
     */
    [[nodiscard]] QJsonObject toJson() const
    {
        QJsonObject j;
        j[QStringLiteral("enabled")] = enabled;
        j[QStringLiteral("attribute")] = attribute;
        j[QStringLiteral("transform")] = transformToString(transform);
        j[QStringLiteral("rangeMode")] = rangeModeToString(rangeMode);
        j[QStringLiteral("userMin")] = userMin;
        j[QStringLiteral("userMax")] = userMax;
        j[QStringLiteral("zeroBased")] = zeroBased;
        j[QStringLiteral("minPx")] = minPx;
        j[QStringLiteral("maxPx")] = maxPx;
        return j;
    }

    /**
     * @brief Restore a size channel with defaults for absent or wrongly typed JSON fields.
     * @param[in] j Project JSON object; unknown fields are ignored and input is not changed.
     * @param[in] areaScaledForKind Geometry archetype's area-scaling rule, independent of
     *                              any value present in j.
     * @return A new channel with the decoded values. Defaults are disabled, empty
     *         attribute, linear transform, run-wide range, [0,1] user range, zero-based
     *         sizing, and [2,14] screen pixels. Numeric ranges are not validated or clamped.
     */
    [[nodiscard]] static SizeChannel fromJson(const QJsonObject &j, bool areaScaledForKind)
    {
        SizeChannel c;
        c.areaScaled = areaScaledForKind;
        c.enabled = j.value(QStringLiteral("enabled")).toBool(false);
        c.attribute = j.value(QStringLiteral("attribute")).toString();
        c.transform = transformFromString(j.value(QStringLiteral("transform")).toString());
        c.rangeMode = rangeModeFromString(j.value(QStringLiteral("rangeMode")).toString());
        c.userMin = j.value(QStringLiteral("userMin")).toDouble(0.0);
        c.userMax = j.value(QStringLiteral("userMax")).toDouble(1.0);
        c.zeroBased = j.value(QStringLiteral("zeroBased")).toBool(true);
        c.minPx = j.value(QStringLiteral("minPx")).toDouble(2.0);
        c.maxPx = j.value(QStringLiteral("maxPx")).toDouble(14.0);
        return c;
    }

    /**
     * @brief Compare every persisted setting and the derived area-scaling flag exactly.
     * @param[in] a Left channel; not modified.
     * @param[in] b Right channel; not modified.
     * @return True when all fields compare equal, using exact floating-point equality
     *         rather than a tolerance. A NaN field therefore prevents equality.
     */
    friend bool operator==(const SizeChannel &a, const SizeChannel &b)
    {
        return a.enabled == b.enabled && a.attribute == b.attribute && a.transform == b.transform &&
               a.rangeMode == b.rangeMode && a.userMin == b.userMin && a.userMax == b.userMax &&
               a.zeroBased == b.zeroBased && a.minPx == b.minPx && a.maxPx == b.maxPx &&
               a.areaScaled == b.areaScaled;
    }
    /**
     * @brief Test whether two channels differ under the full equality contract.
     * @param[in] a Left channel.
     * @param[in] b Right channel.
     * @return The logical negation of operator==, including its exact numeric comparison.
     */
    friend bool operator!=(const SizeChannel &a, const SizeChannel &b) { return !(a == b); }
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_SIZECHANNEL_H
