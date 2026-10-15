// GPL-3.0-or-later
#include "map/flowparticlenode.h"
#include <QSGMaterial>
#include <QSGMaterialShader>
#include <QSGTexture>
#include <QQuickWindow>
#include <QElapsedTimer>
#include <algorithm>
#include <cstring>
#include <cmath>

namespace {
using Vertex=OpenSWMM::Render::FlowParticleVertex;
const QSGGeometry::AttributeSet &attributes() {
    static const QSGGeometry::Attribute attrs[]={
        QSGGeometry::Attribute::create(0,2,QSGGeometry::FloatType,true),
        QSGGeometry::Attribute::create(1,2,QSGGeometry::FloatType),
        QSGGeometry::Attribute::create(2,2,QSGGeometry::FloatType),
        QSGGeometry::Attribute::createWithAttributeType(3,4,QSGGeometry::UnsignedByteType,QSGGeometry::ColorAttribute)};
    static const QSGGeometry::AttributeSet set={4,sizeof(Vertex),attrs};return set;
}
class Material : public QSGMaterial {
public:
    Material() {setFlag(Blending,true);setFlag(RequiresFullMatrix,true);}
    ~Material() override {delete timing;}
    QSGTexture *timing=nullptr;
    float interpolation=1,clock=0,tail=1,width=1.5,head=2,fade=.5;
    float opacity=1,softFade=1,taper=1;
    QPointF anchorOffset;
    QSGMaterialType *type() const override {static QSGMaterialType t;return &t;}
    int compare(const QSGMaterial *other) const override {
        if(this==other)return 0;return std::less<const QSGMaterial*>{}(this,other)?-1:1;
    }
    QSGMaterialShader *createShader(QSGRendererInterface::RenderMode) const override;
};
class Shader : public QSGMaterialShader {
public:
    Shader() {
        setShaderFileName(VertexStage,QStringLiteral(":/resources/shaders/flowparticle.vert.qsb"));
        setShaderFileName(FragmentStage,QStringLiteral(":/resources/shaders/flowparticle.frag.qsb"));
    }
    bool updateUniformData(RenderState &state,QSGMaterial *material,QSGMaterial *) override {
        const auto *m=static_cast<Material*>(material);auto *buf=state.uniformData();
        if(state.isMatrixDirty())std::memcpy(buf->data(),state.combinedMatrix().constData(),64);
        const float values[]={state.opacity()*m->opacity,m->interpolation,m->clock,m->tail,m->width,m->head,m->fade,m->softFade,m->taper};
        std::memcpy(buf->data()+64,values,sizeof(values));
        const float anchor[]={float(m->anchorOffset.x()),float(m->anchorOffset.y())};
        std::memcpy(buf->data()+104,anchor,sizeof(anchor));
        const float rows=m->timing?float(m->timing->textureSize().height()):1;
        std::memcpy(buf->data()+112,&rows,sizeof(rows));return true;
    }
    void updateSampledImage(RenderState &state,int binding,QSGTexture **texture,QSGMaterial *material,QSGMaterial *) override {
        if(binding!=1)return;
        auto *timing=static_cast<Material*>(material)->timing;
        if(timing) {timing->commitTextureOperations(state.rhi(),state.resourceUpdateBatch());*texture=timing;}
    }
};
QSGMaterialShader *Material::createShader(QSGRendererInterface::RenderMode) const {return new Shader;}
}
FlowParticleNode::FlowParticleNode(QQuickWindow *window):m_window(window) {
    auto *g=new QSGGeometry(attributes(),0,0,QSGGeometry::UnsignedIntType);g->setDrawingMode(QSGGeometry::DrawTriangleStrip);
    g->setVertexDataPattern(QSGGeometry::DynamicPattern);
    setGeometry(g);setFlag(OwnsGeometry);setMaterial(new Material);setFlag(OwnsMaterial);
}
void FlowParticleNode::sync(std::shared_ptr<const OpenSWMM::Render::FlowParticleFrame> frame,
    double interpolation,double scale,double tail,double width,double head,double fade,quint64 trackedId,
    const FlowParticleAppearance &appearance) {
    // An already empty node has no uniforms or geometry to present. In the
    // separate-flow composition it lives inside the cached water subtree.
    if(!frame && !m_frame && geometry()->vertexCount()==0)return;
    auto *m=static_cast<Material*>(material());m->interpolation=float(interpolation);
    m->clock=frame?float(frame->previousClock+(frame->clock-frame->previousClock)*interpolation):0;
    m->tail=float(tail);m->width=float(width);m->head=float(head);m->fade=float(fade);
    m->opacity=float(appearance.opacity);m->softFade=appearance.fade?1:0;m->taper=appearance.taper?1:0;
    markDirty(DirtyMaterial);
    if(frame==m_frame && scale==m_scale && trackedId==m_trackedId && appearance.anchor==m_anchor
       && appearance.color==m_color && appearance.revision==m_appearanceRevision && appearance.viewport==m_viewport)return;
    QElapsedTimer build;build.start();
    const auto previous=m_frame;
    m_frame=std::move(frame);m_scale=scale;m_trackedId=trackedId;
    m_anchor=appearance.anchor;m_color=appearance.color;m_appearanceRevision=appearance.revision;m_viewport=appearance.viewport;
    const OpenSWMM::Render::FlowParticleRibbon *ribbon=nullptr;
    if(m_frame && m_frame->ribbon.scale==scale && m_frame->ribbon.revision==appearance.revision
       && m_frame->ribbon.color==appearance.color && m_frame->ribbon.viewport==appearance.viewport
       && m_frame->ribbon.trackedId==trackedId)ribbon=&m_frame->ribbon;
    else if(m_frame) {
        auto bounded=appearance;
        if(m_frame->ribbon.scale>0) {
            // A changed view may need local geometry before the worker's next
            // packet. Keep its population/history shape: carrying the old long
            // history into a newly dense packet multiplies CPU and GPU storage.
            bounded.particleCapacity=std::max(bounded.particleCapacity,int(m_frame->particles.size()));
            bounded.historyCapacity=std::max(bounded.historyCapacity,m_frame->ribbon.history);
            for(const auto &p:m_frame->particles)
                bounded.historyCapacity=std::max(bounded.historyCapacity,int(p.history.size()));
        }
        OpenSWMM::Render::buildFlowParticleRibbon(*m_frame,scale,trackedId,bounded,m_ribbon,previous.get());ribbon=&m_ribbon;
    }
    const int vertices=ribbon?int(ribbon->vertices.size()):0;
    m->anchorOffset=ribbon?ribbon->anchor-appearance.anchor:QPointF();
    if(vertices!=geometry()->vertexCount())geometry()->allocate(vertices);
    if(ribbon && !ribbon->vertices.empty())
        std::memcpy(geometry()->vertexData(),ribbon->vertices.data(),ribbon->vertices.size()*sizeof(Vertex));
    if(m_window && ribbon && !ribbon->timing.empty()) {
        // Preserve raw RGBA8 bytes: Qt uploads this format directly, without
        // premultiplication or colour conversion. The worker's storage stays
        // immutable; the texture owns its small render-thread copy.
        const QImage data(reinterpret_cast<const uchar*>(ribbon->timing.data()),8,ribbon->particles,
                          32,QImage::Format_RGBA8888_Premultiplied);
        delete m->timing;m->timing=m_window->createTextureFromImage(data.copy());
        if(m->timing)m->timing->setFiltering(QSGTexture::Nearest);
    }
    m_geometryBuildMs=build.nsecsElapsed()/1e6;++m_uploads;geometry()->markVertexDataDirty();markDirty(DirtyGeometry);
}
