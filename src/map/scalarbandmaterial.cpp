// GPL-3.0-or-later

/**
 * @file      scalarbandmaterial.cpp
 * @brief     Scene graph material that classifies an interpolated scalar into filled contour bands.
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
constexpr int kMatrix = 0, kOpacity = 64, kLower = 68, kCutoff = 72, kCount = 76, kLevels = 80,
              kColors = kLevels + 16 * ScalarBandMaterial::kMaxBands,
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
                    count = float(material->interiorLevels().size());
        std::memcpy(data + kLower, &lower, 4);
        std::memcpy(data + kCutoff, &cutoff, 4);
        std::memcpy(data + kCount, &count, 4);
        const auto &levels = material->interiorLevels();
        for (size_t i = 0; i < levels.size(); ++i)
            std::memcpy(data + kLevels + 16 * int(i), &levels[i], 4); // .x of a vec4
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
    m_lowerLimit = float(std::max(levels.front(), cutoff));
    m_cutoff = float(cutoff);
    m_interior.assign(levels.begin() + 1, levels.end() - 1);
    m_colors = colors;
    return true;
}
