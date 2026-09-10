/*!
 * \file   multibandcolorrenderer.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Explicit RGB(A) band-composite raster renderer.
 *
 *         Replaces GISRasterLayer's former "three or more bands ⇒ RGB"
 *         hard-wire with a renderer the user can see and switch away from:
 *         it names which bands feed the red / green / blue (/ alpha)
 *         channels, and installing any other IRasterRenderer on a
 *         multi-band raster puts the layer back on the single-band colour
 *         path for the chosen render band.
 *
 *         No per-pixel colour mapping happens here — the warp reads the
 *         bands as bytes and composites them directly — so colorForValue()
 *         is unused (returns transparent). No contrast stretch (deferred).
 */

#ifndef OPENSWMM_RENDER_MULTIBANDCOLORRENDERER_H
#define OPENSWMM_RENDER_MULTIBANDCOLORRENDERER_H

#include "render/irasterrenderer.h"

namespace OpenSWMM::Render
{

/*!
 * \class MultiBandColorRenderer
 * \brief RGB(A) composite from three (or four) raster bands.
 */
class MultiBandColorRenderer final : public IRasterRenderer
{
public:
    MultiBandColorRenderer() = default;
    /*! \param alpha 0 = no alpha band (fully opaque). Bands are 1-based. */
    MultiBandColorRenderer(int red, int green, int blue, int alpha = 0);
    ~MultiBandColorRenderer() override = default;

    [[nodiscard]] int redBand()   const { return m_red; }
    [[nodiscard]] int greenBand() const { return m_green; }
    [[nodiscard]] int blueBand()  const { return m_blue; }
    [[nodiscard]] int alphaBand() const { return m_alpha; }   /*!< 0 = none. */
    void setBands(int red, int green, int blue, int alpha = 0);

    // IRasterRenderer.
    [[nodiscard]] QString rendererId() const override
    {
        return QStringLiteral("multibandcolor");
    }
    [[nodiscard]] QColor colorForValue(double value,
                                       bool isNoData = false) const override;
    [[nodiscard]] QList<LegendSymbolItem> legendSymbolItems() const override;
    [[nodiscard]] QJsonObject toJson() const override;
    void fromJson(const QJsonObject &j) override;
    [[nodiscard]] std::unique_ptr<IRasterRenderer> clone() const override;

private:
    int m_red   = 1;
    int m_green = 2;
    int m_blue  = 3;
    int m_alpha = 0;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_MULTIBANDCOLORRENDERER_H
