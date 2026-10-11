// GPL-3.0-or-later
#include "layers/swmm2dresultslayer.h"
#include "mesh/meshcellgeom.h"
#include <QJsonDocument>
#include <QPainter>
#include <QPen>
#include <algorithm>
#include <cmath>

using namespace OpenSWMM::Render;

bool SWMM2DResultsLayer::flowStreaksEnabled() const
{
    return isVisible() && opacity()>0 && have_velocity_ && m_flowStreakSublayer->isVisible()
        && m_flowStreakSublayer->opacity()>0 && source_
        && (!source_->isLive() || liveRenderEnabled());
}

bool SWMM2DResultsLayer::prepareFlowStreaksForCapture(const QRectF &view,double scale)
{
    setHydraulicView(view,scale);
    // Settle the requested surface without dispatching hydraulic ticks or other
    // application events. The queued duplicate completion is safely ignored.
    const auto displayPending=[&] {
        return pending_surface_ || (active_surface_ && !active_surface_->forEnvelope);
    };
    for(int attempt=0;attempt<3 && displayPending();++attempt) {
        if(pending_surface_) pending_surface_->live=false;
        scheduleFit_();
        if(const auto job=active_surface_) {
            surface_future_.waitForFinished();onSurfaceFinished_(job);
        }
    }
    if(displayPending()) return false;
    if(!flowStreaksEnabled() || !view.intersects(m_sceneBBox)) return true;
    // A cancelled source/CRS job must release its mesh before a replacement
    // can be captured. No application events are dispatched while waiting.
    m_flowCache->waitForDone();
    auto batch=flowStreaks(view,scale,frame_revision_);
    if(!batch) {
        m_flowCache->waitForDone();
        batch=flowStreaks(view,scale,frame_revision_);
    }
    return bool(batch);
}
QString SWMM2DResultsLayer::flowStreakStatus() const
{
    if(!source_) return tr("No 2D results available");
    if(source_->isLive() && !liveRenderEnabled()) return tr("Enable Live 2D to update live flow streaks");
    if(!have_velocity_) return tr("No wet velocity data available for this frame");
    if(!m_flowStreakSublayer->isVisible()) return tr("Show Flow Streaks to start rendering");
    return m_flowCache->status();
}
void SWMM2DResultsLayer::advanceFlowStreaks(double seconds)
{
    const auto *style=m_flowStreakSublayer->streakStyle();
    if(!flowStreaksEnabled() || !style->animated() || style->speedMultiplier()<=0) return;
    if(!std::isfinite(seconds) || seconds<=0) return;
    m_flowSeconds+=std::min(seconds,0.1)*style->speedMultiplier();
    emit flowStreaksChanged();
}

std::shared_ptr<const FlowStreakBatch> SWMM2DResultsLayer::flowStreaks(
    const QRectF &view,double scale,quint64 displayedRevision)
{
    if(!flowStreaksEnabled() || !have_velocity_ || !view.isValid()
       || !std::isfinite(scale) || scale<=0) return {};
    if(m_flowGeomRevision!=geomRevision()) {
        m_flowCache->clear();m_flowMesh.reset();
        m_flowGeomRevision=geomRevision();m_flowCoverage={};
        m_flowFrameKeys.clear();m_flowKey.clear();
    }
    // A complete contour frame may lag the layer: never attach newer flow
    // to that older water surface. An already prepared matching batch survives.
    const auto *style=m_flowStreakSublayer->streakStyle();
    const double bucket=std::exp2(std::floor(std::log2(scale)*4)/4);
    const auto visible=view.intersected(m_sceneBBox);
    if(!visible.isValid()) return {};
    if(displayedRevision!=frame_revision_) {
        for(auto it=m_flowFrameKeys.rbegin();it!=m_flowFrameKeys.rend();++it)
            if(it->revision==displayedRevision && it->scale==bucket
               && it->settings==style->pathSettings() && it->coverage.contains(visible))
                if(auto batch=m_flowCache->lookup(it->key)) return batch;
        return {};
    }
    // A source/CRS replacement cancels the old worker. Let it release its
    // regional snapshot before allocating replacement geometry.
    if(!m_flowMesh && m_flowCache->busy()) return {};
    if(!m_flowCoverage.contains(visible) || m_flowScale!=bucket) {
        m_flowScale=bucket;
        const double pad=std::max(view.width()*0.5,style->pathLengthPx()/bucket);
        m_flowSeedArea=(view.width()+2*pad)*(view.height()+2*pad);
        m_flowCoverage=view.adjusted(-pad,-pad,pad,pad).intersected(m_sceneBBox);
    }
    const auto key=published_key_.frame>=0?published_key_:surfaceKey_(current_time_idx_);
    QJsonObject json=style->pathSettings();
    json["source"]=QString::number(key.sourceRev);json["geometry"]=QString::number(key.geomRev);
    json["region"]=QString::number(key.regionRev);
    json["policy"]=QString::number(key.policyRev);json["generation"]=key.generation;
    json["frame"]=key.frame;json["tick"]=key.tickRev;json["scale"]=bucket;
    json["x"]=m_flowCoverage.x();json["y"]=m_flowCoverage.y();
    json["w"]=m_flowCoverage.width();json["h"]=m_flowCoverage.height();
    m_flowKey=QJsonDocument(json).toJson(QJsonDocument::Compact);
    if(m_flowFrameKeys.empty() || m_flowFrameKeys.back().key!=m_flowKey
       || m_flowFrameKeys.back().revision!=displayedRevision) {
        if(m_flowFrameKeys.size()>=32) m_flowFrameKeys.erase(m_flowFrameKeys.begin());
        m_flowFrameKeys.push_back({displayedRevision,m_flowKey,style->pathSettings(),m_flowCoverage,bucket});
    }
    m_flowCache->request(m_flowKey,[this,style,bucket] {
        FlowStreakInput in;in.coverage=m_flowCoverage;in.seedBudgetArea=m_flowSeedArea;
        in.spacing=style->spacingPx()/bucket;in.maxLength=style->pathLengthPx()/bucket;
        in.step=6./bucket;in.cutoff=style->dryDepthCutoff();
        if(!m_flowMesh || !m_flowMesh->coverage.contains(m_flowCoverage)
           || (m_flowMesh->triangles.empty() && m_flowMesh->coverage!=m_flowCoverage)) {
            auto mesh=qEnvironmentVariableIsSet("OPENSWMM_FLOW_OVERVIEW")?nullptr
                :FlowStreakMesh::captureRegion(m_sceneVerts,tris_,m_triGrid,m_flowCoverage);
            if(!mesh) {
                // Remember the bounded region-query miss. Hydraulic updates
                // reuse native overview sampling without enumerating it again.
                mesh=std::make_shared<FlowStreakMesh>();mesh->coverage=m_flowCoverage;
            }
            m_flowMesh=std::move(mesh);
        }
        if(m_flowMesh->triangles.empty()) {
            in.sampler=flowSampler_(in.cutoff,in.spacing);return in;
        }
        const auto &ids=m_flowMesh->sourceTriangles;
        // Dry/stagnant cells cannot seed or carry a path. Keep their topology
        // (so boundaries remain conservative), but do not duplicate their
        // fields in active/pending snapshots for a large model overview.
        const auto flowing=[&](int i) {
            const auto &t=m_sceneTris[i];
            return std::isfinite(t.vmag) && t.vmag>1e-8
                && std::isfinite(t.dv0) && std::isfinite(t.dv1) && std::isfinite(t.dv2)
                && std::max({t.dv0,t.dv1,t.dv2})>in.cutoff;
        };
        const size_t fieldCount=size_t(std::count_if(ids.begin(),ids.end(),flowing));
        if(!FlowStreakCache::fitsBudget(m_flowMesh->bytes(),fieldCount)) return in;
        in.mesh=m_flowMesh;
        in.triangles.reserve(fieldCount);
        for(int local=0;local<int(ids.size());++local)
            if(flowing(ids[size_t(local)])) in.triangles.push_back(local);
        in.fields.reserve(in.triangles.size());
        for(int local:in.triangles) {
            in.fields.push_back(flowField_(ids[size_t(local)]));
        }
        return in;
    });
    return m_flowCache->lookup(m_flowKey);
}

FlowStreakField SWMM2DResultsLayer::flowField_(int i) const
{
    const auto &t=m_sceneTris[i];const auto &vi=tris_[size_t(i)];
    FlowStreakField f;f.depth={t.dv0,t.dv1,t.dv2};f.speed=t.vmag;
    // Map an SI displacement through the same affine triangle map
    // used by the displayed mesh (including feet, CRS and Y inversion).
    const QPointF a(vx_[vi[0]],vy_[vi[0]]),b(vx_[vi[1]],vy_[vi[1]]),c(vx_[vi[2]],vy_[vi[2]]);
    const QPointF ab=b-a,ac=c-a,v(t.vx,-t.vy);
    const double det=ab.x()*ac.y()-ab.y()*ac.x();
    if(std::isfinite(det) && std::abs(det)>1e-20) {
        const double u=(v.x()*ac.y()-v.y()*ac.x())/det;
        const double w=(ab.x()*v.y()-ab.y()*v.x())/det;
        const QPointF sv=(t.b-t.a)*u+(t.c-t.a)*w;
        f.vx=float(sv.x());f.vy=float(sv.y());
    }
    const int cell=triCell_[size_t(i)];const auto &cv=cells_[size_t(cell)];
    const int nv=cv[3]>=0?4:3;
    for(int e=0;e<3;++e) {
        const int aId=vi[(e+1)%3],bId=vi[(e+2)%3];int slot=-1;
        for(int k=0;k<nv;++k) {
            const int u=cv[(k+1)%nv],vId=cv[(k+2)%nv];
            if((u==aId && vId==bId)||(u==bId && vId==aId)) {slot=mesh::edgeSlot(cell,k);break;}
        }
        // A quad's display diagonal has no hydraulic boundary. A
        // physical edge only permits outward-positive reported flow.
        if(slot<0 || (size_t(slot)<current_flux_.size()
           && std::isfinite(current_flux_[size_t(slot)]) && current_flux_[size_t(slot)]>0))
            f.exits|=1<<e;
    }
    return f;
}

bool SWMM2DResultsLayer::flowSample_(int i,FlowStreakSample &s) const
{
    if(i<0 || i>=m_sceneTris.size()) return false;
    s.id=i;s.field=flowField_(i);s.neighbors={-1,-1,-1};
    const auto &tri=tris_[size_t(i)];
    for(int k=0;k<3;++k)s.vertices[k]=m_sceneVerts[tri[k]];
    const int cell=triCell_[size_t(i)];const auto &cv=cells_[size_t(cell)];
    const int nv=cv[3]>=0?4:3;
    // Use the already validated global topology. Cropping/sampling must never
    // manufacture a manifold edge by missing a third (possibly dry) face.
    if(surfaceNeighbors_.size()!=cells_.size()) return true;
    for(int e=0;e<3;++e) {
        const int a=tri[(e+1)%3],b=tri[(e+2)%3];int other=cell;
        for(int k=0;k<nv;++k) if((cv[k]==a && cv[(k+1)%nv]==b) || (cv[k]==b && cv[(k+1)%nv]==a)) {
            other=surfaceNeighbors_[size_t(cell)][k];break;
        }
        if(other<0) continue;
        for(int t=cellTri0_[size_t(other)];t<cellTri0_[size_t(other)+1];++t) if(t!=i) {
            const auto &v=tris_[size_t(t)];
            if(std::find(v.begin(),v.end(),a)!=v.end() && std::find(v.begin(),v.end(),b)!=v.end()) {
                s.neighbors[e]=t;break;
            }
        }
    }
    return true;
}

FlowStreakSampler SWMM2DResultsLayer::flowSampler_(double cutoff,double spacing)
{
    FlowStreakSampler sampler;sampler.stats=std::make_shared<FlowStreakSampler::Stats>();
    const auto revision=frame_revision_,geometry=geomRevision();const auto key=m_flowKey;
    sampler.valid=[this,revision,geometry,key] {
        return frame_revision_==revision && geomRevision()==geometry && m_flowKey==key && flowStreaksEnabled();
    };
    sampler.cell=[this](int i,FlowStreakSample &s) {return flowSample_(i,s);};
    sampler.locate=[this,cutoff,spacing,stats=sampler.stats](QPointF &p,FlowStreakSample &s) {
        const int *begin=nullptr,*end=nullptr;m_triGrid.candidatesAtPoint(p.x(),p.y(),begin,end);
        // Never scan a pathological index bucket or the full domain. An
        // ambiguous/exhausted seed is omitted; native tracing never jumps it.
        constexpr int perSeed=8192,total=2000000;
        if(!begin || end-begin>perSeed || stats->candidateTests>=total) {stats->limited=true;return false;}
        int nearby=-1;QPointF nearbyPoint;double best=spacing*spacing*.25;
        for(const int *it=begin;it!=end;++it) {
            if(stats->candidateTests>=total) {stats->limited=true;return false;}
            ++stats->candidateTests;
            const auto &t=m_sceneTris[*it];
            if(!std::isfinite(t.vmag) || t.vmag<=1e-8 || !std::isfinite(t.dv0)
               || !std::isfinite(t.dv1) || !std::isfinite(t.dv2)) continue;
            const QPointF ab=t.b-t.a,ac=t.c-t.a,ap=p-t.a;
            const double det=ab.x()*ac.y()-ab.y()*ac.x();if(std::abs(det)<1e-20) continue;
            const double v=(ap.x()*ac.y()-ap.y()*ac.x())/det,w=(ab.x()*ap.y()-ab.y()*ap.x())/det;
            if(v>=-1e-9 && w>=-1e-9 && 1-v-w>=-1e-9 && (1-v-w)*t.dv0+v*t.dv1+w*t.dv2>cutoff)
                return flowSample_(*it,s);
            // Native refinement for a narrow wet channel missed by a seed:
            // relocate to an actual wet triangle, never an averaged vector.
            if((double(t.dv0)+t.dv1+t.dv2)/3<=cutoff) continue;
            const QPointF center=(t.a+t.b+t.c)/3.,d=center-p;const double d2=d.x()*d.x()+d.y()*d.y();
            if(d2<best) {nearby=*it;nearbyPoint=center;best=d2;}
        }
        if(nearby<0) return false;
        p=nearbyPoint;return flowSample_(nearby,s);
    };
    return sampler;
}

void SWMM2DResultsLayer::paintFlowStreaks(QPainter *p,const QRectF &view)
{
    const double scale=std::hypot(p->worldTransform().m11(),p->worldTransform().m12());
    // QGraphicsScene::render can expose the whole item during its first
    // fallback paint. Respect the actual painter clip before requesting a
    // region, including during image capture and the handoff to QSG.
    QRectF coverage=view.isValid()?view:m_sceneBBox;
    if(p->hasClipping()) coverage=coverage.intersected(p->clipBoundingRect());
    if(!coverage.isValid()) return;
    const auto batch=flowStreaks(coverage,scale,frame_revision_);
    if(!batch) return;
    const auto *style=m_flowStreakSublayer->streakStyle();
    p->save();p->setOpacity(p->opacity()*m_flowStreakSublayer->opacity());
    p->setRenderHint(QPainter::Antialiasing,true);
    QPen pen;pen.setCosmetic(true);pen.setCapStyle(Qt::FlatCap);
    for(const auto &path:batch->paths) {
        const double duration=path.points.back().seconds;
        const double tail=duration*std::min(1.,style->trailLengthPx()/(path.length*scale));
        for(size_t i=1;i<path.points.size();++i) {
            const auto &a=path.points[i-1],&b=path.points[i];
            const double alpha=flowStreakOpacity((a.seconds+b.seconds)*0.5,duration,tail,path.offset,m_flowSeconds,style->fade());
            const double taper=flowStreakOpacity((a.seconds+b.seconds)*0.5,duration,tail,path.offset,m_flowSeconds,true);
            if(alpha<=0) continue;
            QColor color=style->colorForSpeed(b.speed);color.setAlphaF(color.alphaF()*alpha);
            pen.setColor(color);pen.setWidthF(style->widthAtFraction(taper));
            p->setPen(pen);p->drawLine(a.position,b.position);
        }
    }p->restore();
}
