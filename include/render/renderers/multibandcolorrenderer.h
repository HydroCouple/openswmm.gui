/**
 * @file      multibandcolorrenderer.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Explicit RGB(A) band-composite raster renderer.
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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
    /** @brief Construct RGB bands 1,2,3 with no alpha band. */
    MultiBandColorRenderer() = default;
    /** @brief Store an explicit composite-band selection through setBands().
     * @param red One-based red band, clamped to at least one.
     * @param green One-based green band, clamped to at least one.
     * @param blue One-based blue band, clamped to at least one.
     * @param alpha One-based alpha band, or zero for none; negatives clamp to zero.
     * Band availability and duplicate channel selections are not validated. */
    MultiBandColorRenderer(int red, int green, int blue, int alpha = 0);
    /** @brief Destroy this metadata-only renderer. */
    ~MultiBandColorRenderer() override = default;

    /** @brief Read the red channel's source band.
     * @return Stored one-based index, without checking a particular raster. */
    [[nodiscard]] int redBand() const { return m_red; }
    /** @brief Read the green channel's source band.
     * @return Stored one-based index. */
    [[nodiscard]] int greenBand() const { return m_green; }
    /** @brief Read the blue channel's source band.
     * @return Stored one-based index. */
    [[nodiscard]] int blueBand() const { return m_blue; }
    /** @brief Read the optional alpha channel's source band.
     * @return Stored one-based index, or zero when no alpha band is requested. */
    [[nodiscard]] int alphaBand() const { return m_alpha; } /*!< 0 = none. */
    /** @brief Replace composite-band selections without reading raster data.
     * @param red One-based red band clamped to at least one.
     * @param green One-based green band clamped to at least one.
     * @param blue One-based blue band clamped to at least one.
     * @param alpha Optional alpha band clamped to at least zero; zero means none.
     * No upper limit, uniqueness check or repaint notification is applied. */
    void setBands(int red, int green, int blue, int alpha = 0);

    // IRasterRenderer.
    /** @brief Identify the persisted renderer type.
     * @return Exact token multibandcolor. */
    [[nodiscard]] QString rendererId() const override { return QStringLiteral("multibandcolor"); }
    /** @brief Provide the scalar-interface fallback for a byte-band composite.
     * @param value Unused scalar; no band sampling or conversion occurs.
     * @param isNoData Unused flag; both true and false return transparent.
     * @return Transparent unconditionally. The layer's warp/composite path reads
     * configured source bands directly to obtain actual display colors. */
    [[nodiscard]] QColor colorForValue(double value, bool isNoData = false) const override;
    /** @brief Describe the RGB source selection with a generic swatch.
     * @return One gray SimpleFill row labelled with RGB band indices; alpha is
     * omitted from the label and no per-value class keys or numeric ranges are supplied. */
    [[nodiscard]] QList<LegendSymbolItem> legendSymbolItems() const override;
    /** @brief Serialize all four band indices with the renderer discriminator.
     * @return Owned object; source-raster availability is not checked. */
    [[nodiscard]] QJsonObject toJson() const override;
    /** @brief Replace channel selections from JSON using constructor defaults.
     * @param j Object whose id is ignored. Missing/mistyped channels become
     * RGB 1,2,3 and alpha zero; setBands() applies its lower-bound clamping. */
    void fromJson(const QJsonObject &j) override;
    /** @brief Copy the channel configuration into an independent renderer.
     * @return Sole ownership of the new metadata snapshot. */
    [[nodiscard]] std::unique_ptr<IRasterRenderer> clone() const override;

private:
    int m_red = 1;
    int m_green = 2;
    int m_blue = 3;
    int m_alpha = 0;
};

} // namespace OpenSWMM::Render

#endif // OPENSWMM_RENDER_MULTIBANDCOLORRENDERER_H
