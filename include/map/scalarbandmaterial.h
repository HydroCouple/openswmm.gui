// GPL-3.0-or-later

/**
 * @file      scalarbandmaterial.h
 * @brief     Scene graph material that draws filled contours or isolines of an interpolated scalar.
 *
 * Depth contours on the fast vertex surface: each vertex carries its depth (ScalarFillVertex) and
 * the fragment stage works on the linearly interpolated value, so nothing is marched on the CPU.
 *
 * Bands: the same boundaries as marching triangles (OpenSWMM::Contour::forEachTriangleIsoband).
 * Values below the first level or at/below the cutoff are not drawn, the band is the number of
 * interior levels at or below the value, and the last band saturates above its top level.
 *
 * Isolines: a fragment within a level's half line width (in screen pixels, from the value's
 * screen-space gradient) takes the line color, antialiased over one pixel.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#ifndef OPENSWMM_MAP_SCALARBANDMATERIAL_H
#define OPENSWMM_MAP_SCALARBANDMATERIAL_H

#include <QSGMaterial>

#include <array>
#include <vector>

class ScalarBandMaterial : public QSGMaterial
{
public:
    static constexpr int kMaxBands = 256;

    ScalarBandMaterial();

    [[nodiscard]] QSGMaterialType *type() const override;
    /** @return Pointer order: every node keeps its own upload batch (16-bit indices). */
    [[nodiscard]] int compare(const QSGMaterial *other) const override;
    [[nodiscard]] QSGMaterialShader *createShader(QSGRendererInterface::RenderMode) const override;

    /**
     * @brief Draw filled contours.
     * @param[in] levels Ascending band breaks (N breaks, N-1 bands, 2 <= N <= kMaxBands + 1).
     * @param[in] cutoff Physical cutoff; values at or below it are not drawn.
     * @param[in] colors Premultiplied RGBA in [0, 1], one per band (N-1 entries).
     * @return False (material unchanged) for fewer than two breaks, too many bands or a color
     *   count that does not match.
     */
    bool setBands(const std::vector<double> &levels, double cutoff,
                  const std::vector<std::array<float, 4>> &colors);
    /**
     * @brief Draw isolines.
     * @param[in] levels Line values (1..kMaxBands).
     * @param[in] halfWidthsPx Half line width in pixels, one per level.
     * @param[in] color Premultiplied RGBA in [0, 1].
     * @return False (material unchanged) for no or too many levels or mismatched widths.
     */
    bool setIsolines(const std::vector<double> &levels, const std::vector<float> &halfWidthsPx,
                     const std::array<float, 4> &color);

    [[nodiscard]] bool isolines() const { return m_isolines; }
    [[nodiscard]] float lowerLimit() const { return m_lowerLimit; }
    [[nodiscard]] float cutoff() const { return m_cutoff; }
    /** @return Bands: interior breaks levels[1..N-2]. Isolines: the line values. */
    [[nodiscard]] const std::vector<float> &levels() const { return m_levels; }
    /** @return Isolines: half widths in pixels, parallel to levels(); empty for bands. */
    [[nodiscard]] const std::vector<float> &halfWidthsPx() const { return m_halfWidths; }
    /** @return Bands: one color per band. Isolines: the line color. */
    [[nodiscard]] const std::vector<std::array<float, 4>> &colors() const { return m_colors; }

private:
    bool m_isolines = false;
    float m_lowerLimit = 0, m_cutoff = 0;
    std::vector<float> m_levels, m_halfWidths;
    std::vector<std::array<float, 4>> m_colors;
};

#endif // OPENSWMM_MAP_SCALARBANDMATERIAL_H
