/*!
 * \file   scalarfillmaterial.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * QSG-2D-1M Phase 8 — see scalarfillmaterial.h. The .qsb shaders are
 * compiled by qt_add_shaders from resources/shaders/scalarfill.{vert,frag}.
 */
#include "map/scalarfillmaterial.h"

#include <QSGMaterialShader>
#include <QSGTexture>

#include <cstring>

const QSGGeometry::AttributeSet &scalarFillAttributes()
{
    static const QSGGeometry::Attribute attrs[] = {
        QSGGeometry::Attribute::createWithAttributeType(
            0, 2, QSGGeometry::FloatType, QSGGeometry::PositionAttribute),
        QSGGeometry::Attribute::createWithAttributeType(
            1, 1, QSGGeometry::FloatType, QSGGeometry::UnknownAttribute),
    };
    static const QSGGeometry::AttributeSet set = {2, sizeof(ScalarFillVertex),
                                                  attrs};
    return set;
}

namespace {

class ScalarFillShader : public QSGMaterialShader
{
public:
    ScalarFillShader()
    {
        setShaderFileName(VertexStage,
                          QLatin1String(":/resources/shaders/scalarfill.vert.qsb"));
        setShaderFileName(FragmentStage,
                          QLatin1String(":/resources/shaders/scalarfill.frag.qsb"));
    }

    bool updateUniformData(RenderState &state,
                           QSGMaterial *newMaterial, QSGMaterial *) override
    {
        // std140 layout of `buf`:
        //   0..63  mat4  qt_Matrix
        //   64     float qt_Opacity
        //   68     float vMin
        //   72     float vInvRange
        QByteArray *buf = state.uniformData();
        Q_ASSERT(buf->size() >= 76);
        bool changed = false;
        if (state.isMatrixDirty()) {
            std::memcpy(buf->data(), state.combinedMatrix().constData(), 64);
            changed = true;
        }
        if (state.isOpacityDirty()) {
            const float o = state.opacity();
            std::memcpy(buf->data() + 64, &o, 4);
            changed = true;
        }
        auto *mat = static_cast<ScalarFillMaterial *>(newMaterial);
        const float vMin = mat->vMin();
        const float inv  = mat->invRange();
        std::memcpy(buf->data() + 68, &vMin, 4);
        std::memcpy(buf->data() + 72, &inv, 4);
        return changed || true;   // range is cheap; always push
    }

    void updateSampledImage(RenderState &state, int binding,
                            QSGTexture **texture,
                            QSGMaterial *newMaterial, QSGMaterial *) override
    {
        if (binding != 1) return;
        auto *mat = static_cast<ScalarFillMaterial *>(newMaterial);
        if (QSGTexture *t = mat->rampTexture()) {
            t->commitTextureOperations(state.rhi(), state.resourceUpdateBatch());
            *texture = t;
        }
    }
};

} // namespace

ScalarFillMaterial::ScalarFillMaterial()
{
    setFlag(Blending, true);   // LUT colors carry alpha (sublayer opacity)
}

ScalarFillMaterial::~ScalarFillMaterial()
{
    delete m_texture;
}

QSGMaterialType *ScalarFillMaterial::type() const
{
    static QSGMaterialType kType;
    return &kType;
}

int ScalarFillMaterial::compare(const QSGMaterial *other) const
{
    const auto *o = static_cast<const ScalarFillMaterial *>(other);
    if (m_texture != o->m_texture)
        return m_texture < o->m_texture ? -1 : 1;
    if (m_vMin != o->m_vMin)
        return m_vMin < o->m_vMin ? -1 : 1;
    if (m_invRange != o->m_invRange)
        return m_invRange < o->m_invRange ? -1 : 1;
    return 0;
}

QSGMaterialShader *
ScalarFillMaterial::createShader(QSGRendererInterface::RenderMode) const
{
    return new ScalarFillShader();
}

void ScalarFillMaterial::setRampTexture(QSGTexture *texture)
{
    if (m_texture == texture) return;
    delete m_texture;
    m_texture = texture;
}

void ScalarFillMaterial::setRange(float vMin, float vMax)
{
    m_vMin     = vMin;
    m_invRange = (vMax > vMin) ? 1.0f / (vMax - vMin) : 0.0f;
}
