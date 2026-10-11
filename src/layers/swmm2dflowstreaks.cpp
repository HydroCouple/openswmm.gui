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
    // topology before allocating another whole-mesh adjacency snapshot.
    if(!m_flowMesh && m_flowCache->busy()) return {};
    if(!m_flowCoverage.contains(visible) || m_flowScale!=bucket) {
        m_flowScale=bucket;
        const double pad=std::max(view.width()*0.5,style->pathLengthPx()/bucket);
        m_flowSeedArea=(view.width()+2*pad)*(view.height()+2*pad);
        m_flowCoverage=view.adjusted(-pad,-pad,pad,pad).intersected(m_sceneBBox);
    }
    const auto key=surfaceKey_(current_time_idx_);
    QJsonObject json=style->pathSettings();
    json["source"]=QString::number(key.sourceRev);json["geometry"]=QString::number(key.geomRev);
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
        const auto ids=m_triGrid.query(m_flowCoverage);
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
        // Check the peak before copying topology or allocating field snapshots.
        const size_t meshBytes=size_t(m_sceneVerts.capacity())*sizeof(QPointF)
            +tris_.size()*60+size_t(m_triGrid.cellOffsets.capacity()+m_triGrid.cellIndices.capacity())*sizeof(int)
            +m_triGrid.seen.capacity()*sizeof(quint32);
        if(!FlowStreakCache::fitsBudget(meshBytes,fieldCount)) return in;
        if(!m_flowMesh) {
            m_flowMesh=std::make_shared<FlowStreakMesh>();
            m_flowMesh->vertices=m_sceneVerts;m_flowMesh->triangles=tris_;
            m_flowMesh->grid=m_triGrid;m_flowMesh->grid.seen.clear();
        }
        in.mesh=m_flowMesh;
        in.triangles.reserve(fieldCount);
        for(int i:ids) if(flowing(i)) in.triangles.push_back(i);
        std::sort(in.triangles.begin(),in.triangles.end());
        in.fields.reserve(in.triangles.size());
        for(int i:in.triangles) {
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
            in.fields.push_back(f);
        }
        return in;
    });
    return m_flowCache->lookup(m_flowKey);
}

void SWMM2DResultsLayer::paintFlowStreaks(QPainter *p,const QRectF &view)
{
    const double scale=std::hypot(p->worldTransform().m11(),p->worldTransform().m12());
    const auto batch=flowStreaks(view.isValid()?view:m_sceneBBox,scale,frame_revision_);
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
            pen.setColor(color);pen.setWidthF(style->widthPx()*(style->taper()?std::max(0.15,taper):1.));
            p->setPen(pen);p->drawLine(a.position,b.position);
        }
    }p->restore();
}
