// GPL-3.0-or-later

/**
 * @file      flowparticlenode.cpp
 * @brief     Scene graph node for animated flow particles.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "map/flowparticlenode.h"
#include <QSGMaterial>
#include <QSGMaterialShader>
#include <QSGTexture>
#include <QQuickWindow>
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <algorithm>
#include <cstring>
#include <cmath>
Q_DECLARE_LOGGING_CATEGORY(lcFlowPerf)

namespace
{
using Vertex = OpenSWMM::Render::FlowParticleVertex;
/**
 * @return Borrowed static28byteparticle vertex layout: vec2position, vec2normal-offset,
 *   vec2sample-age/particle-row and normalized RGBA8colour. Do not mutate/free; matches
 *   flowparticle shaders.
 */
const QSGGeometry::AttributeSet &attributes()
{
    static const QSGGeometry::Attribute attrs[] = {
        QSGGeometry::Attribute::create(0, 2, QSGGeometry::FloatType, true),
        QSGGeometry::Attribute::create(1, 2, QSGGeometry::FloatType),
        QSGGeometry::Attribute::create(2, 2, QSGGeometry::FloatType),
        QSGGeometry::Attribute::createWithAttributeType(3, 4, QSGGeometry::UnsignedByteType,
                                                        QSGGeometry::ColorAttribute)};
    static const QSGGeometry::AttributeSet set = {4, sizeof(Vertex), attrs};
    return set;
}
class Material : public QSGMaterial
{
public:
    /**
     * @brief Initialize blended full-matrix particle material; timing texture null, display
     *   uniforms at field defaults. Render-thread object.
     */
    Material()
    {
        setFlag(Blending, true);
        setFlag(RequiresFullMatrix, true);
    }
    /**
     * @brief Delete owned timing texture on scene-graph render thread; node/base owns material
     *   lifetime.
     */
    ~Material() override { delete timing; }
    QSGTexture *timing = nullptr;
    float interpolation = 1, clock = 0, tail = 1, width = 1.5, head = 2, fade = .5;
    float opacity = 1, softFade = 1, taper = 1;
    QPointF anchorOffset;
    /**
     * @return Borrowed static type identity for particle material; never delete/mutate.
     */
    QSGMaterialType *type() const override
    {
        static QSGMaterialType t;
        return &t;
    }
    /**
     * @param[in] other Required borrowed peer material pointer from same QSG family.
     * @return 0only same object, otherwise strict pointer-address ordering via std::less. Does not
     *   batch distinct equal-state materials or mutate resources.
     */
    int compare(const QSGMaterial *other) const override
    {
        if (this == other)
            return 0;
        return std::less<const QSGMaterial *>{}(this, other) ? -1 : 1;
    }
    /**
     * @brief Allocate particle Shader; RenderMode argument ignored.
     * @return New shader owned by Qt renderer; resource creation/context handled by QSG.
     */
    QSGMaterialShader *createShader(QSGRendererInterface::RenderMode) const override;
};
class Shader : public QSGMaterialShader
{
public:
    /**
     * @brief Select compiled flowparticle vertex/fragment shader resources; no texture allocation
     *   or geometry mutation.
     */
    Shader()
    {
        setShaderFileName(VertexStage, QStringLiteral(":/resources/shaders/flowparticle.vert.qsb"));
        setShaderFileName(FragmentStage,
                          QStringLiteral(":/resources/shaders/flowparticle.frag.qsb"));
    }
    /**
     * @param[in,out] state Render state/buffer, caller guarantees at least116bytes std140 storage.
     * @param[in] material Required actual particle Material, unchecked static cast.
     * @return True always after writes. Copies64bytematrix only dirty, then writes
     *   opacity×appearanceOpacity, interpolation/clock/tail/width/head/fade/flags, anchor offset
     *   and timing row count. Unnamed previous-material argument ignored. Render-thread only; no
     *   resource ownership transfer.
     */
    bool updateUniformData(RenderState &state, QSGMaterial *material, QSGMaterial *) override
    {
        const auto *m = static_cast<Material *>(material);
        auto *buf = state.uniformData();
        if (state.isMatrixDirty())
            std::memcpy(buf->data(), state.combinedMatrix().constData(), 64);
        const float values[] = {state.opacity() * m->opacity,
                                m->interpolation,
                                m->clock,
                                m->tail,
                                m->width,
                                m->head,
                                m->fade,
                                m->softFade,
                                m->taper};
        std::memcpy(buf->data() + 64, values, sizeof(values));
        const float anchor[] = {float(m->anchorOffset.x()), float(m->anchorOffset.y())};
        std::memcpy(buf->data() + 104, anchor, sizeof(anchor));
        const float rows = m->timing ? float(m->timing->textureSize().height()) : 1;
        std::memcpy(buf->data() + 112, &rows, sizeof(rows));
        return true;
    }
    /**
     * @param[in,out] state Active RHI/resource batch for timing texture commit.
     * @param[in] binding Shader sampler slot; only1handled.
     * @param[out] texture Required output pointer for binding1; unchanged if no timing texture.
     * @param[in] material Required particle Material, borrowed/unchecked cast.
     * @note Commit retained timing texture and return borrowed sampler when present; no fallback
     *   allocation. Unnamed previous material ignored, render-thread call.
     */
    void updateSampledImage(RenderState &state, int binding, QSGTexture **texture,
                            QSGMaterial *material, QSGMaterial *) override
    {
        if (binding != 1)
            return;
        auto *timing = static_cast<Material *>(material)->timing;
        if (timing) {
            timing->commitTextureOperations(state.rhi(), state.resourceUpdateBatch());
            *texture = timing;
        }
    }
};
QSGMaterialShader *Material::createShader(QSGRendererInterface::RenderMode) const
{
    return new Shader;
}
}
FlowParticleNode::FlowParticleNode(QQuickWindow *window) : m_window(window)
{
    auto *g = new QSGGeometry(attributes(), 0, 0, QSGGeometry::UnsignedIntType);
    g->setDrawingMode(QSGGeometry::DrawTriangleStrip);
    g->setVertexDataPattern(QSGGeometry::DynamicPattern);
    setGeometry(g);
    setFlag(OwnsGeometry);
    setMaterial(new Material);
    setFlag(OwnsMaterial);
}
void FlowParticleNode::sync(std::shared_ptr<const OpenSWMM::Render::FlowParticleFrame> frame,
                            double interpolation, double scale, double tail, double width,
                            double head, double fade, quint64 trackedId,
                            const FlowParticleAppearance &appearance)
{
    // An already empty node has no uniforms or geometry to present. In the
    // separate-flow composition it lives inside the cached water subtree.
    if (!frame && !m_frame && geometry()->vertexCount() == 0)
        return;
    auto *m = static_cast<Material *>(material());
    m->interpolation = float(interpolation);
    m->clock =
        frame ? float(frame->previousClock + (frame->clock - frame->previousClock) * interpolation)
              : 0;
    m->tail = float(tail);
    m->width = float(width);
    m->head = float(head);
    m->fade = float(fade);
    m->opacity = float(appearance.opacity);
    m->softFade = appearance.fade ? 1 : 0;
    m->taper = appearance.taper ? 1 : 0;
    markDirty(DirtyMaterial);
    if (frame == m_frame && scale == m_scale && trackedId == m_trackedId &&
        appearance.anchor == m_anchor && appearance.color == m_color &&
        appearance.revision == m_appearanceRevision && appearance.viewport == m_viewport)
        return;
    // A trackpad pinch changes the scale slightly on every rendered frame while
    // one publication is shown. Keep that geometry for a single frame (width
    // error under 10 %); the next publication or sync rebuilds it.
    const bool pinchStep = frame == m_frame && m_frame && scale != m_scale && m_scale > 0 &&
                           std::abs(scale - m_scale) <= .1 * m_scale && trackedId == m_trackedId &&
                           appearance.anchor == m_anchor && appearance.color == m_color &&
                           appearance.revision == m_appearanceRevision;
    if (pinchStep && !m_deferredView) {
        m_deferredView = true;
        return;
    }
    m_deferredView = false;
    QElapsedTimer build;
    build.start();
    const auto previous = m_frame;
    m_frame = std::move(frame);
    m_scale = scale;
    m_trackedId = trackedId;
    m_anchor = appearance.anchor;
    m_color = appearance.color;
    m_appearanceRevision = appearance.revision;
    m_viewport = appearance.viewport;
    const OpenSWMM::Render::FlowParticleRibbon *ribbon = nullptr;
    if (m_frame && m_frame->ribbon.scale == scale &&
        m_frame->ribbon.revision == appearance.revision &&
        m_frame->ribbon.color == appearance.color &&
        m_frame->ribbon.viewport == appearance.viewport && m_frame->ribbon.trackedId == trackedId)
        ribbon = &m_frame->ribbon;
    else if (m_frame) {
        auto bounded = appearance;
        if (m_frame->ribbon.scale > 0) {
            // A changed view may need local geometry before the worker's next
            // packet. Keep its population/history shape: carrying the old long
            // history into a newly dense packet multiplies CPU and GPU storage.
            bounded.particleCapacity =
                std::max(bounded.particleCapacity, int(m_frame->particles.size()));
            bounded.historyCapacity = std::max(bounded.historyCapacity, m_frame->ribbon.history);
            for (const auto &p : m_frame->particles)
                bounded.historyCapacity = std::max(bounded.historyCapacity, int(p.history.size()));
        }
        OpenSWMM::Render::buildFlowParticleRibbon(*m_frame, scale, trackedId, bounded, m_ribbon,
                                                  previous.get());
        ribbon = &m_ribbon;
    }
    const int vertices = ribbon ? int(ribbon->vertices.size()) : 0;
    m->anchorOffset = ribbon ? ribbon->anchor - appearance.anchor : QPointF();
    if (vertices != geometry()->vertexCount())
        geometry()->allocate(vertices);
    if (ribbon && !ribbon->vertices.empty())
        std::memcpy(geometry()->vertexData(), ribbon->vertices.data(),
                    ribbon->vertices.size() * sizeof(Vertex));
    if (m_window && ribbon && !ribbon->timing.empty()) {
        // Preserve raw RGBA8 bytes: Qt uploads this format directly, without
        // premultiplication or colour conversion. The worker's storage stays
        // immutable; the texture owns its small render-thread copy.
        const QImage data(reinterpret_cast<const uchar *>(ribbon->timing.data()), 8,
                          ribbon->particles, 32, QImage::Format_RGBA8888_Premultiplied);
        delete m->timing;
        m->timing = m_window->createTextureFromImage(data.copy());
        if (m->timing)
            m->timing->setFiltering(QSGTexture::Nearest);
    }
    m_geometryBuildMs = build.nsecsElapsed() / 1e6;
    ++m_uploads;
    geometry()->markVertexDataDirty();
    markDirty(DirtyGeometry);
    qCDebug(lcFlowPerf) << "flow ribbon" << (m_frame != previous ? "new frame" : "view change")
                        << vertices << "vertices" << m_geometryBuildMs << "ms";
}
