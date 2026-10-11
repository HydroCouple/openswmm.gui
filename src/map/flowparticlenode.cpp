// GPL-3.0-or-later
#include "map/flowparticlenode.h"
#include <QSGMaterial>
#include <QSGMaterialShader>
#include <QElapsedTimer>
#include <cstring>
#include <cmath>

namespace {
struct Vertex { float x,y,nx,ny,age,previous,current,lifetime,side,stoppedAt,r,g,b; };
const QSGGeometry::AttributeSet &attributes() {
    static const QSGGeometry::Attribute attrs[]={
        QSGGeometry::Attribute::create(0,2,QSGGeometry::FloatType,true),
        QSGGeometry::Attribute::create(1,2,QSGGeometry::FloatType),
        QSGGeometry::Attribute::create(2,4,QSGGeometry::FloatType),
        QSGGeometry::Attribute::create(3,2,QSGGeometry::FloatType),
        QSGGeometry::Attribute::create(4,3,QSGGeometry::FloatType)};
    static const QSGGeometry::AttributeSet set={5,sizeof(Vertex),attrs};return set;
}
class Material : public QSGMaterial {
public:
    Material() {setFlag(Blending,true);setFlag(RequiresFullMatrix,true);}
    float interpolation=1,clock=0,tail=1,width=1.5,head=2,fade=.5;
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
        const float values[]={state.opacity(),m->interpolation,m->clock,m->tail,m->width,m->head,m->fade};
        std::memcpy(buf->data()+64,values,sizeof(values));return true;
    }
};
QSGMaterialShader *Material::createShader(QSGRendererInterface::RenderMode) const {return new Shader;}
QPointF unit(QPointF p) {const double length=std::hypot(p.x(),p.y());return length>1e-12?p/length:QPointF();}
}
FlowParticleNode::FlowParticleNode() {
    auto *g=new QSGGeometry(attributes(),0);g->setDrawingMode(QSGGeometry::DrawTriangles);
    g->setVertexDataPattern(QSGGeometry::DynamicPattern);
    setGeometry(g);setFlag(OwnsGeometry);setMaterial(new Material);setFlag(OwnsMaterial);
}
void FlowParticleNode::sync(std::shared_ptr<const OpenSWMM::Render::FlowParticleFrame> frame,
    double interpolation,double scale,double tail,double width,double head,double fade,quint64 trackedId) {
    auto *m=static_cast<Material*>(material());m->interpolation=float(interpolation);
    m->clock=frame?float(frame->clock):0;m->tail=float(tail);m->width=float(width);m->head=float(head);m->fade=float(fade);
    markDirty(DirtyMaterial);
    if(frame==m_frame && scale==m_scale && trackedId==m_trackedId)return;
    QElapsedTimer build;build.start();
    m_frame=std::move(frame);m_scale=scale;m_trackedId=trackedId;
    int count=0;if(m_frame && scale>0)for(const auto &p:m_frame->particles)
        if(p.history.size()>1)count+=int(p.history.size()-1)*6;
    geometry()->allocate(count);auto *v=static_cast<Vertex*>(geometry()->vertexData());
    if(m_frame && scale>0)for(const auto &p:m_frame->particles) {
        const auto &history=p.history;
        auto make=[&](size_t i) {
            const auto point=history[i].position;
            const auto before=unit(point-history[i?i-1:i].position);
            const auto after=unit(history[std::min(i+1,history.size()-1)].position-point);
            const auto direction=unit(before+after);QPointF normal(-direction.y(),direction.x());
            const QPointF sideNormal(-after.y(),after.x());
            const double projection=std::abs(QPointF::dotProduct(normal,sideNormal));
            // Shared offset at each joint; limit long miters at sharp bends.
            normal*=std::min(2.,projection>1e-12?1/projection:1.)*(.5/scale);
            Vertex left{float(point.x()),float(point.y()),float(-normal.x()),float(-normal.y()),
                float(history[i].age),float(p.previousAge),float(p.age),float(p.lifetime),-1,float(p.stoppedAt),
                1.f,p.id==trackedId?.45f:1.f,p.id==trackedId?.1f:1.f};
            Vertex right=left;right.nx=-left.nx;right.ny=-left.ny;right.side=1;
            return std::array<Vertex,2>{left,right};
        };
        if(history.empty())continue;
        auto previous=make(0);
        for(size_t i=1;i<history.size();++i) {
            const auto next=make(i);const auto &al=previous[0],&ar=previous[1],&bl=next[0],&br=next[1];
            *v++=al;*v++=ar;*v++=br;*v++=al;*v++=br;*v++=bl;
            previous=next;
        }
    }
    m_geometryBuildMs=build.nsecsElapsed()/1e6;++m_uploads;geometry()->markVertexDataDirty();markDirty(DirtyGeometry);
}
