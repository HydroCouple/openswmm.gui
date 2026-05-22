/*!
 * \file   singlebandpseudocolorrenderer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Raster renderer that maps a single-band value to a colour ramp.
 *
 *         SingleBandPseudoColorRenderer is the IRasterRenderer counterpart
 *         to GraduatedRenderer on the feature side.  It carries:
 *           - minValue / maxValue — the value range covered by the ramp
 *           - stops               — sorted list of (pos [0..1], QColor)
 *           - clampMin / clampMax — when true, out-of-range values are
 *                                   rendered transparent (matches the
 *                                   existing RasterColorRamp policy on
 *                                   GISRasterLayer)
 *
 *         v1 uses linear interpolation between adjacent stops only.  A
 *         later slice can introduce log / discrete interpolation modes;
 *         the public colorForValue / legendSymbolItems contract does not
 *         change at that swap.
 *
 *         Cross-slice: Slice BI Phase 8.13.6 (see GUI_IMPLEMENTATION_PLAN.md
 *         §J.2). Sub-phase 8.13.6.7 — concrete raster renderer class.
 */

#ifndef OPENSWMM_RENDER_SINGLEBANDPSEUDOCOLORRENDERER_H
#define OPENSWMM_RENDER_SINGLEBANDPSEUDOCOLORRENDERER_H

#include "render/irasterrenderer.h"

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

    SingleBandPseudoColorRenderer() = default;
    ~SingleBandPseudoColorRenderer() override = default;

    /*!
     * \brief The numeric range covered by the ramp.
     *        Values outside [minValue, maxValue] are clamped to the
     *        nearest stop unless clampMin / clampMax are true (in which
     *        case they render as transparent).
     */
    [[nodiscard]] double minValue() const { return m_minValue; }
    [[nodiscard]] double maxValue() const { return m_maxValue; }
    void setRange(double minValue, double maxValue);

    /*!
     * \brief Stops in ascending-position order.  Callers must keep this
     *        invariant; setStops() does not sort.
     */
    [[nodiscard]] const QList<Stop> &stops() const { return m_stops; }
    void setStops(QList<Stop> stops);

    /*!
     * \brief When true, values strictly below minValue render as transparent
     *        instead of clamping to the first stop's colour.
     */
    [[nodiscard]] bool clampMin() const { return m_clampMin; }
    void setClampMin(bool clamp) { m_clampMin = clamp; }

    /*!
     * \brief When true, values strictly above maxValue render as transparent
     *        instead of clamping to the last stop's colour.
     */
    [[nodiscard]] bool clampMax() const { return m_clampMax; }
    void setClampMax(bool clamp) { m_clampMax = clamp; }

    // IRasterRenderer.
    [[nodiscard]] QString rendererId() const override
    {
        return QStringLiteral("singlebandpseudocolor");
    }
    [[nodiscard]] QColor colorForValue(double value,
                                       bool isNoData = false) const override;
    [[nodiscard]] QList<LegendSymbolItem> legendSymbolItems() const override;
    [[nodiscard]] QJsonObject toJson() const override;
    void fromJson(const QJsonObject &j) override;
    [[nodiscard]] std::unique_ptr<IRasterRenderer> clone() const override;

private:
    double        m_minValue = 0.0;
    double        m_maxValue = 1.0;
    QList<Stop>   m_stops;
    bool          m_clampMin = false;
    bool          m_clampMax = false;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_SINGLEBANDPSEUDOCOLORRENDERER_H
