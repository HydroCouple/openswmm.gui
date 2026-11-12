// GPL-3.0-or-later

/**
 * @file      scalarbandmaterial.h
 * @brief     Scene graph material that classifies an interpolated scalar into filled contour bands.
 *
 * Filled depth contours on the fast vertex surface: each vertex carries its depth
 * (ScalarFillVertex) and the fragment stage picks the band of the interpolated value. On a
 * triangle with linearly interpolated values this draws the same band boundaries as marching
 * triangles (OpenSWMM::Contour::forEachTriangleIsoband) without building any polygons on the CPU:
 * values below the first level or at/below the dry cutoff are not drawn, the band is the number
 * of interior levels at or below the value, and the last band saturates above its top level.
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
     * @brief Set the classification.
     * @param[in] levels Ascending band breaks (N breaks, N-1 bands, 2 <= N <= kMaxBands + 1).
     * @param[in] cutoff Physical cutoff; values at or below it are not drawn.
     * @param[in] colors Premultiplied RGBA in [0, 1], one per band (N-1 entries).
     * @return False (material unchanged) for fewer than two breaks, too many bands or a colour
     *   count that does not match.
     */
    bool setBands(const std::vector<double> &levels, double cutoff,
                  const std::vector<std::array<float, 4>> &colors);

    [[nodiscard]] float lowerLimit() const { return m_lowerLimit; }
    [[nodiscard]] float cutoff() const { return m_cutoff; }
    /** @return Interior breaks levels[1..N-2], at most kMaxBands - 1. */
    [[nodiscard]] const std::vector<float> &interiorLevels() const { return m_interior; }
    [[nodiscard]] const std::vector<std::array<float, 4>> &colors() const { return m_colors; }

private:
    float m_lowerLimit = 0, m_cutoff = 0;
    std::vector<float> m_interior;
    std::vector<std::array<float, 4>> m_colors;
};

#endif // OPENSWMM_MAP_SCALARBANDMATERIAL_H
