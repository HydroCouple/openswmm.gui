/**
 * @file      singlebandpseudocolorrenderer.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Raster renderer that maps a single-band value to a colour ramp.
 *
 *         SingleBandPseudoColorRenderer is the IRasterRenderer counterpart
 *         to GraduatedRenderer on the feature side.  It carries:
 *           - minValue / maxValue — the value range covered by the ramp
 *           - stops               — sorted list of (pos [0..1], QColor)
 *           - clampMin / clampMax — legacy serialized flags retained for round trips.
 *                                   Underflow is hidden; overflow saturates.
 *
 *         Adjacent colors interpolate in RGB or along the selected HSV arc.
 *         Callers supply ordered stops; setters and loading do not sort them.
 *
 *         Cross-slice: Slice BI Phase 8.13.6 (see GUI_IMPLEMENTATION_PLAN.md
 *         §J.2). Sub-phase 8.13.6.7 — concrete raster renderer class.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMM_RENDER_SINGLEBANDPSEUDOCOLORRENDERER_H
#define OPENSWMM_RENDER_SINGLEBANDPSEUDOCOLORRENDERER_H

#include "render/irasterrenderer.h"
#include "render/colorramp.h" // RampInterp — match legacy ramp fidelity (P5/R-1)

#include <QColor>
#include <QList>
#include <QPair>

namespace OpenSWMM::Render
{

/*!
 * \class SingleBandPseudoColorRenderer
 * \brief Continuous colour ramp for single-band raster data.
 */
class SingleBandPseudoColorRenderer final : public IRasterRenderer
{
public:
    /*!
     * \brief One ramp stop — a normalised position in [0,1] paired with a colour.
     */
    using Stop = QPair<double, QColor>;

    /** @brief Construct an empty, transparent ramp over [0,1] with RGB interpolation. */
    SingleBandPseudoColorRenderer() = default;
    /** @brief Destroy the stored value-type stops and configuration. */
    ~SingleBandPseudoColorRenderer() override = default;

    /*!
     * \brief The numeric range covered by the ramp.
     *        Values below minValue are transparent; values above maxValue
     *        use the final stop.
     * @return Stored lower bound in raster-band units.
     */
    [[nodiscard]] double minValue() const { return m_minValue; }
    /** @brief Read the upper ramp bound.
     * @return Stored scalar in the same raster-band units as minValue(). */
    [[nodiscard]] double maxValue() const { return m_maxValue; }
    /** @brief Replace the data range without validation or notifications.
     * @param minValue Finite lower bound in raw raster-band units.
     * @param maxValue Finite upper bound in the same units. Nonincreasing bounds
     * are accepted and use the first-stop policy for values not below minimum. */
    void setRange(double minValue, double maxValue);

    /*!
     * \brief Stops in ascending-position order.  Callers must keep this
     *        invariant; setStops() does not sort.
     * @return Borrowed list valid for this renderer's lifetime; setters/loading can replace it.
     */
    [[nodiscard]] const QList<Stop> &stops() const { return m_stops; }
    /** @brief Replace stop storage without sorting, validation or notifications.
     * @param stops Ordered finite normalized positions in [0,1] and valid colors,
     * moved into storage. Empty is legal and renders transparent. Callers must
     * maintain ordering; a nonincreasing selected pair uses its lower color. */
    void setStops(QList<Stop> stops);

    /*!
     * \brief Legacy serialized flag; underflow is always transparent.
     * @return Stored compatibility metadata, ignored by colorForValue().
     */
    [[nodiscard]] bool clampMin() const { return m_clampMin; }
    /** @brief Retain the old minimum-clamping flag for persistence.
     * @param clamp Metadata value to store; numeric underflow remains transparent. */
    void setClampMin(bool clamp) { m_clampMin = clamp; }

    /*!
     * \brief Legacy serialized flag; overflow always saturates.
     * @return Stored compatibility metadata, ignored by colorForValue().
     */
    [[nodiscard]] bool clampMax() const { return m_clampMax; }
    /** @brief Retain the old maximum-clamping flag for persistence.
     * @param clamp Metadata value to store; high values still saturate to the ramp end. */
    void setClampMax(bool clamp) { m_clampMax = clamp; }

    /*!
     * \brief Colour-space used between adjacent stops (Rgb / HsvShort /
     *        HsvLong). Carried so the renderer reproduces the legacy
     *        RasterColorRamp output byte-for-byte after the P5 full switch
     *        — without it, custom HSV ramps would drift to RGB lerp.
     * @return Stored interpolation selection, without validation.
     */
    [[nodiscard]] RampInterp interp() const { return m_interp; }
    /** @brief Store an interpolation mode without validation or notifications.
     * @param i RGB, short HSV arc or long HSV arc; unknown enum values sample as RGB. */
    void setInterp(RampInterp i) { m_interp = i; }

    // IRasterRenderer.
    /** @brief Identify the persisted renderer type.
     * @return Exact token singlebandpseudocolor. */
    [[nodiscard]] QString rendererId() const override
    {
        return QStringLiteral("singlebandpseudocolor");
    }
    /** @brief Map a raw band value through ordered stops and the chosen color space.
     * @param value Raw scalar in the finite configured bounds' units.
     * @param isNoData True to force transparent, regardless of value or stops.
     * @return Transparent for no-data, nonfinite input, empty stops or underflow.
     * One stop is constant for visible values; a nonincreasing range returns the
     * first stop. Other values clamp to the normalized endpoints before interpolation.
     * Achromatic endpoints use RGB even in HSV modes; legacy clamp flags are ignored. */
    [[nodiscard]] QColor colorForValue(double value, bool isNoData = false) const override;
    /** @brief Build one reference legend row per stored stop.
     * @return Owned rows in stop order, labelled min+position*(max-min) with Qt's
     * default number format, a SimpleFill swatch, and NaN ranges. No sorting occurs. */
    [[nodiscard]] QList<LegendSymbolItem> legendSymbolItems() const override;
    /** @brief Serialize range, stops, compatibility flags and interpolation mode.
     * @return Owned JSON object; colors use ARGB text and no values are validated. */
    [[nodiscard]] QJsonObject toJson() const override;
    /** @brief Replace every ramp setting using defaults for absent/mistyped fields.
     * @param j Object whose id is ignored. Missing bounds become [0,1], stops empty,
     * flags false and mode RGB. Invalid color strings become transparent. Stop
     * positions/order and bounds are not validated; unknown interpolation integers
     * are retained and fall back to RGB when sampling. */
    void fromJson(const QJsonObject &j) override;
    /** @brief Copy all ramp configuration into an independent renderer.
     * @return Sole ownership of the new renderer snapshot. */
    [[nodiscard]] std::unique_ptr<IRasterRenderer> clone() const override;

private:
    double m_minValue = 0.0;
    double m_maxValue = 1.0;
    QList<Stop> m_stops;
    bool m_clampMin = false;
    bool m_clampMax = false;
    RampInterp m_interp = RampInterp::Rgb;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_SINGLEBANDPSEUDOCOLORRENDERER_H
