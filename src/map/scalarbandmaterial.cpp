// GPL-3.0-or-later

/**
 * @file      scalarbandmaterial.cpp
 * @brief     Scene graph material that draws filled contours or isolines of an interpolated scalar.
 *
 * See scalarbandmaterial.h. The .qsb shaders are compiled by qt_add_shaders from
 * resources/shaders/scalarband.{vert,frag}.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "map/scalarbandmaterial.h"

#include <QSGMaterialShader>

#include <algorithm>
#include <cstring>
#include <functional>

namespace
{
// std140 layout of `buf` in scalarband.{vert,frag}.
constexpr int kMatrix = 0, kOpacity = 64, kLower = 68, kCutoff = 72, kCount = 76, kMode = 80,
              kLevels = 96, kColors = kLevels + 16 * ScalarBandMaterial::kMaxBands,
              kSize = kColors + 16 * ScalarBandMaterial::kMaxBands;

class ScalarBandShader : public QSGMaterialShader
{
public:
    /** @brief Select the compiled scalarband vertex/fragment resources. */
    ScalarBandShader()
    {
        setShaderFileName(VertexStage, QLatin1String(":/resources/shaders/scalarband.vert.qsb"));
        setShaderFileName(FragmentStage, QLatin1String(":/resources/shaders/scalarband.frag.qsb"));
    }

    /**
     * @param[in,out] state Uniform buffer of at least kSize bytes, matrix/opacity dirty flags.
     * @param[in] newMaterial Required ScalarBandMaterial.
     * @return True: the classification is rewritten on every call (small, and materials of
     *   different nodes share one shader instance).
     */
    bool updateUniformData(RenderState &state, QSGMaterial *newMaterial, QSGMaterial *) override
    {
        QByteArray *buf = state.uniformData();
        Q_ASSERT(buf->size() >= kSize);
        char *data = buf->data();
        if (state.isMatrixDirty())
            std::memcpy(data + kMatrix, state.combinedMatrix().constData(), 64);
        if (state.isOpacityDirty()) {
            const float opacity = state.opacity();
            std::memcpy(data + kOpacity, &opacity, 4);
        }
        const auto *material = static_cast<const ScalarBandMaterial *>(newMaterial);
        const float lower = material->lowerLimit(), cutoff = material->cutoff(),
                    count = float(material->levels().size()),
                    mode = material->isolines() ? 1.f : 0.f;
        std::memcpy(data + kLower, &lower, 4);
        std::memcpy(data + kCutoff, &cutoff, 4);
        std::memcpy(data + kCount, &count, 4);
        std::memcpy(data + kMode, &mode, 4);
        const auto &levels = material->levels();
        const auto &widths = material->halfWidthsPx();
        for (size_t i = 0; i < levels.size(); ++i) {
            const float level[2] = {levels[i], i < widths.size() ? widths[i] : 0.f};
            std::memcpy(data + kLevels + 16 * int(i), level, 8); // .x level, .y half width
        }
        const auto &colors = material->colors();
        for (size_t i = 0; i < colors.size(); ++i)
            std::memcpy(data + kColors + 16 * int(i), colors[i].data(), 16);
        return true;
    }
};
} // namespace

ScalarBandMaterial::ScalarBandMaterial()
{
    setFlag(Blending, true);
    setFlag(NoBatching, true);
}

QSGMaterialType *ScalarBandMaterial::type() const
{
    static QSGMaterialType kType;
    return &kType;
}

int ScalarBandMaterial::compare(const QSGMaterial *other) const
{
    if (this == other)
        return 0;
    return std::less<const QSGMaterial *>{}(this, other) ? -1 : 1;
}

QSGMaterialShader *ScalarBandMaterial::createShader(QSGRendererInterface::RenderMode) const
{
    return new ScalarBandShader();
}

bool ScalarBandMaterial::setBands(const std::vector<double> &levels, double cutoff,
                                  const std::vector<std::array<float, 4>> &colors)
{
    if (levels.size() < 2 || levels.size() > size_t(kMaxBands) + 1 ||
        colors.size() != levels.size() - 1)
        return false;
    m_isolines = false;
    m_lowerLimit = float(std::max(levels.front(), cutoff));
    m_cutoff = float(cutoff);
    m_levels.assign(levels.begin() + 1, levels.end() - 1);
    m_halfWidths.clear();
    m_colors = colors;
    return true;
}

bool ScalarBandMaterial::setIsolines(const std::vector<double> &levels,
                                     const std::vector<float> &halfWidthsPx,
                                     const std::array<float, 4> &color)
{
    if (levels.empty() || levels.size() > size_t(kMaxBands) ||
        halfWidthsPx.size() != levels.size())
        return false;
    m_isolines = true;
    m_lowerLimit = m_cutoff = 0;
    m_levels.assign(levels.begin(), levels.end());
    m_halfWidths = halfWidthsPx;
    m_colors = {color};
    return true;
}
