/*!
 * \file   irasterrenderer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Sister interface to IFeatureRenderer for single-band raster layers.
 *
 *         Where IFeatureRenderer maps a (feature, attrs) pair to a
 *         SymbolStyle, IRasterRenderer maps a raw pixel value to a
 *         display QColor.  The narrower domain (single scalar in / one
 *         RGBA colour out) means a much smaller surface than the feature
 *         interface: there is no per-feature attribute map, no symbol
 *         layer stack, no scale-range gate.
 *
 *         Concrete renderers planned by GUI_IMPLEMENTATION_PLAN.md §J.2
 *         (sub-phase 8.13.6.7):
 *           - SingleBandPseudoColorRenderer — interpolated stops + clamp policy
 *           - SingleBandGrayRenderer        — min→black, max→white, optional invert
 *           - PalettedRenderer              — discrete byte-value → colour table
 *           - HillshadeRenderer             — light-direction-shaded relief
 *           - MultiBandColorRenderer        — RGB(A) bypass, no ramp
 *
 *         Sub-phase 8.13.6.7 ships the interface + SingleBandPseudoColorRenderer
 *         stub only; the remaining concrete renderers arrive in later slices
 *         when their corresponding UI is implemented.
 *
 *         Legend pipeline alignment: legendSymbolItems() returns the same
 *         LegendSymbolItem type used by IFeatureRenderer so the legend dock
 *         can render mixed-layer compositions through a single code path
 *         (§J.5 legend-from-renderer rule).
 *
 *         JSON round-trip (toJson / fromJson) is mandatory for .oswp
 *         save/load and .swmm-style.json import/export (Slice BB Phase
 *         8.6.13).  clone() returns a deep copy for map preset capture
 *         (Slice CJ.2) without aliasing the live renderer.
 */

#ifndef OPENSWMM_RENDER_IRASTERRENDERER_H
#define OPENSWMM_RENDER_IRASTERRENDERER_H

#include "render/legendsymbolitem.h"

#include <QColor>
#include <QJsonObject>
#include <QList>
#include <QString>

#include <memory>

namespace OpenSWMM::Render
{

/*!
 * \class IRasterRenderer
 * \brief Abstract interface — every raster layer's paint path goes here.
 *
 *        Implementations must be deterministic: given the same raw pixel
 *        value, colorForValue() must return the same QColor.  This is what
 *        lets the legend pipeline trust legendSymbolItems() as ground truth
 *        for "what the warped raster will show".
 *
 *        Renderers are owned via std::unique_ptr by the raster layer.
 *        Use clone() whenever a snapshot is needed (map presets, undo stack).
 */
class IRasterRenderer
{
public:
    virtual ~IRasterRenderer() = default;

    /*!
     * \brief Stable string id ("singlebandpseudocolor", "singlebandgray",
     *        "paletted", "hillshade", "multibandcolor"). Used as the
     *        discriminator in JSON round-trip.
     */
    [[nodiscard]] virtual QString rendererId() const = 0;

    /*!
     * \brief Returns the display colour for one raw pixel value.
     * \param value    Raw pixel value (from the selected band, in raster
     *                 units — no normalisation has been applied yet).
     * \param isNoData true when the source pixel matched the layer's
     *                 no-data sentinel.  Implementations are expected to
     *                 return Qt::transparent in that case (or whatever
     *                 their no-data policy specifies).
     *
     *        Note: NaN handling is the renderer's responsibility — callers
     *        do not pre-filter NaNs.  Implementations should return
     *        Qt::transparent for non-finite values.
     */
    [[nodiscard]] virtual QColor colorForValue(double value,
                                               bool isNoData = false) const = 0;

    /*!
     * \brief Returns one LegendSymbolItem per visible legend row.
     *        Drives the legend dock, the on-canvas legend overlay, and the
     *        map composer's legend frame (§J.5).
     *
     *        For continuous-ramp renderers, implementations typically emit
     *        one item per stop (label = formatted value, swatch = stop colour).
     */
    [[nodiscard]] virtual QList<LegendSymbolItem> legendSymbolItems() const = 0;

    /*!
     * \brief Serialises this renderer to a JSON object.
     *        The "id" field equals rendererId(); remaining keys are
     *        renderer-specific.
     */
    [[nodiscard]] virtual QJsonObject toJson() const = 0;

    /*!
     * \brief Restores this renderer from a JSON object previously produced
     *        by toJson().  Implementations should tolerate missing/unknown
     *        keys by falling back to default values rather than throwing.
     */
    virtual void fromJson(const QJsonObject &j) = 0;

    /*!
     * \brief Deep copy.  Owning layers / map presets / undo stack call this
     *        when they need a renderer snapshot decoupled from the live one.
     */
    [[nodiscard]] virtual std::unique_ptr<IRasterRenderer> clone() const = 0;

protected:
    IRasterRenderer() = default;
    IRasterRenderer(const IRasterRenderer &) = default;
    IRasterRenderer &operator=(const IRasterRenderer &) = default;
    IRasterRenderer(IRasterRenderer &&) = default;
    IRasterRenderer &operator=(IRasterRenderer &&) = default;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_IRASTERRENDERER_H
