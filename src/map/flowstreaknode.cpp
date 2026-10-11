// GPL-3.0-or-later
#include "map/flowstreaknode.h"
#include <QSGMaterial>
#include <QSGMaterialShader>
#include <cstring>
#include <cmath>

namespace {
struct Vertex { float x,y,nx,ny,t,duration,tail,offset,r,g,b,a; };
const QSGGeometry::AttributeSet &attributes() {
    static const QSGGeometry::Attribute attrs[]={
        QSGGeometry::Attribute::create(0,2,QSGGeometry::FloatType,true),
        QSGGeometry::Attribute::create(1,2,QSGGeometry::FloatType),
        QSGGeometry::Attribute::create(2,4,QSGGeometry::FloatType),
        QSGGeometry::Attribute::create(3,4,QSGGeometry::FloatType)};
    static const QSGGeometry::AttributeSet set={4,sizeof(Vertex),attrs};return set;
}
class Material : public QSGMaterial {
public:
    Material() {
        setFlag(Blending,true);
        // The shader adds a scene-space normal offset before transforming;
        // Qt must not pretransform only the position attribute when batching.
        setFlag(RequiresFullMatrix,true);
    }
    float phase=0,opacity=1,fade=1,taper=1;
    float width=1.5,headWidth=1.5;
    QSGMaterialType *type() const override {static QSGMaterialType t;return &t;}
    int compare(const QSGMaterial *other) const override {
        if(this==other)return 0;return std::less<const QSGMaterial*>{}(this,other)?-1:1;
    }
    QSGMaterialShader *createShader(QSGRendererInterface::RenderMode) const override;
};
class Shader : public QSGMaterialShader {
public:
    Shader() {
        setShaderFileName(VertexStage,QStringLiteral(":/resources/shaders/flowstreak.vert.qsb"));
        setShaderFileName(FragmentStage,QStringLiteral(":/resources/shaders/flowstreak.frag.qsb"));
    }
    bool updateUniformData(RenderState &state,QSGMaterial *m,QSGMaterial *) override {
        auto *material=static_cast<Material*>(m);auto *buf=state.uniformData();
        if(state.isMatrixDirty())std::memcpy(buf->data(),state.combinedMatrix().constData(),64);
        const float values[]={state.opacity()*material->opacity,material->phase,material->fade,material->taper,
                              material->width,material->headWidth};
        std::memcpy(buf->data()+64,values,sizeof(values));return true;
    }
};
QSGMaterialShader *Material::createShader(QSGRendererInterface::RenderMode) const {return new Shader;}
}
FlowStreakNode::FlowStreakNode() {
    auto *g=new QSGGeometry(attributes(),0);g->setDrawingMode(QSGGeometry::DrawTriangles);
    setGeometry(g);setFlag(OwnsGeometry);setMaterial(new Material);setFlag(OwnsMaterial);
}
void FlowStreakNode::sync(std::shared_ptr<const OpenSWMM::Render::FlowStreakBatch> batch,
    const OpenSWMM::Render::FlowStreakStyle &style,double phase,double opacity,QPointF anchor,double scale)
{
    auto *mat=static_cast<Material*>(material());
    mat->phase=float(phase);mat->opacity=float(opacity);mat->fade=style.fade();mat->taper=style.taper();
    mat->width=float(style.widthPx());mat->headWidth=float(style.headWidthPx());
    markDirty(DirtyMaterial);
    auto json=style.toJson();json.remove("animated");json.remove("speedMultiplier");
    json.remove("fade");json.remove("taper");
    json.remove("widthPx");json.remove("headWidthPx");
    if(batch==m_batch && json==m_style && anchor==m_anchor && scale==m_scale) return;
    m_batch=batch;m_style=json;m_anchor=anchor;m_scale=scale;
    int count=0;if(batch) for(const auto &p:batch->paths)count+=int(p.points.size()-1)*6;
    geometry()->allocate(count);auto *v=static_cast<Vertex*>(geometry()->vertexData());
    if(batch && scale>0)for(const auto &path:batch->paths) {
        const float duration=path.points.back().seconds;
        const float tail=duration*float(std::min(1.,style.trailLengthPx()/(path.length*scale)));
        for(size_t i=1;i<path.points.size();++i) {
            const auto &a=path.points[i-1],&b=path.points[i];const QPointF d=b.position-a.position;
            const double len=std::hypot(d.x(),d.y());
            const QPointF n=len>0?QPointF(-d.y(),d.x())*(0.5/(scale*len)):QPointF();
            auto make=[&](const auto &p,double side) {
                const QColor c=style.colorForSpeed(p.speed);const auto q=p.position-anchor;
                return Vertex{float(q.x()),float(q.y()),float(n.x()*side),float(n.y()*side),
                    p.seconds,duration,tail,path.offset,float(c.redF()),float(c.greenF()),float(c.blueF()),float(c.alphaF())};
            };
            const auto al=make(a,-1),ar=make(a,1),bl=make(b,-1),br=make(b,1);
            *v++=al;*v++=ar;*v++=br;*v++=al;*v++=br;*v++=bl;
        }
    }
    ++m_uploads;markDirty(DirtyGeometry);
}
