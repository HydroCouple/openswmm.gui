// GPL-3.0-or-later
#include "layers/swmm2dresultslayer.h"
#include "mesh/meshcellgeom.h"
#include <unordered_map>
#include <unordered_set>

size_t SWMM2DResultsLayer::hydraulicRegionCellCount() const
{
    return surface_region_ ? surface_region_->cells.size() : 0;
}

void SWMM2DResultsLayer::setHydraulicView(const QRectF& view, double scale)
{
    if (!source_ || (source_->isLive() && !live_render_enabled_) || !source_->supportsHydraulicCells() || !view.isValid()
        || !std::isfinite(scale) || scale<=0 || surfaceNeighbors_.size()!=cells_.size()) return;
    if (cells_.size()<=250000 && !qEnvironmentVariableIsSet("OPENSWMM_REGIONAL_SURFACE")) return;
    const QRectF visible=view.intersected(m_sceneBBox);
    if (!visible.isValid()) return;
    const double bucket=std::exp2(std::floor(std::log2(scale)*4)/4);
    const double pathPad=m_flowStreakSublayer->streakStyle()->pathLengthPx()/bucket;
    const auto needed=visible.adjusted(-pathPad,-pathPad,pathPad,pathPad).intersected(m_sceneBBox);
    if(surface_region_ && surface_region_->coverage.contains(needed)) return;
    const double pad=std::max({view.width()*.5,view.height()*.5,2*pathPad});
    const auto coverage=view.adjusted(-pad,-pad,pad,pad).intersected(m_sceneBBox);
    if (hydraulic_view_==coverage) return; // rejected overview, until view/scale changes
    hydraulic_view_=coverage;
    bool complete=false;
    auto triangles=m_triGrid.query(coverage,int(SurfaceRegion::maxCells),&complete);
    auto region=std::make_shared<SurfaceRegion>(); region->coverage=coverage;
    std::unordered_set<int> seen;
    std::vector<int> innerCells;
    if (complete) {
        for (int t:triangles) if (seen.insert(triCell_[size_t(t)]).second)
            region->cells.push_back(triCell_[size_t(t)]);
        region->coreCells=region->cells;
        std::sort(region->coreCells.begin(),region->coreCells.end());
        size_t begin=0;
        // The nonlinear fit is regional, not a truncation claiming exact global
        // equivalence. Keep the displayed cells sixteen manifold edges away
        // from artificial fit boundaries; full-domain analysis stays separate.
        for (int ring=0;ring<16 && complete;++ring) {
            const size_t end=region->cells.size();
            for(size_t i=begin;i<end && complete;++i)
                for(int c:surfaceNeighbors_[size_t(region->cells[i])])
                    if(c>=0 && seen.insert(c).second) {
                        if(region->cells.size()==SurfaceRegion::maxCells) {complete=false;break;}
                        region->cells.push_back(c);
                    }
            begin=end;
            if(ring==7) innerCells=region->cells;
        }
    }
    if (!complete || region->cells.empty() || region->cells.size()==cells_.size()) {
        if (!surface_region_) return;
        surface_region_.reset();
    } else {
        std::sort(region->cells.begin(),region->cells.end());
        region->splits.reserve(region->cells.size());
        std::unordered_map<int,int> local;local.reserve(region->cells.size());
        for(size_t i=0;i<region->cells.size();++i) {
            const int c=region->cells[i];local.emplace(c,int(i));
            region->splits.push_back(cellSplit_[size_t(c)]);
            for(int v:cells_[size_t(c)]) if(v>=0) region->vertices.push_back(v);
        }
        std::sort(region->vertices.begin(),region->vertices.end());
        region->vertices.erase(std::unique(region->vertices.begin(),region->vertices.end()),region->vertices.end());
        for(size_t i=0;i<region->cells.size();++i) {
            const int c=region->cells[i];const auto& a=cells_[size_t(c)];
            const int na=a[3]>=0?4:3;
            for(int k=0;k<na;++k) {
                const int other=surfaceNeighbors_[size_t(c)][k];
                const auto it=local.find(other);
                if(other<=c || it==local.end()) continue;
                const auto& b=cells_[size_t(other)];
                const int p=int(std::find(b.begin(),b.end(),a[k])-b.begin());
                const int q=int(std::find(b.begin(),b.end(),a[(k+1)%na])-b.begin());
                region->topology.edges.push_back({4*int(i)+k,4*it->second+p,
                    4*int(i)+(k+1)%na,4*it->second+q});
            }
        }
        auto check=std::make_shared<SurfaceRegion>();
        check->cells=std::move(innerCells);std::sort(check->cells.begin(),check->cells.end());
        std::unordered_map<int,int> checkLocal;
        for(size_t i=0;i<check->cells.size();++i) {
            checkLocal.emplace(check->cells[i],int(i));check->splits.push_back(cellSplit_[size_t(check->cells[i])]);
        }
        for(const auto& edge:region->topology.edges) {
            const auto a=checkLocal.find(region->cells[size_t(edge.a0/4)]);
            const auto b=checkLocal.find(region->cells[size_t(edge.a1/4)]);
            if(a!=checkLocal.end() && b!=checkLocal.end()) check->topology.edges.push_back({
                4*a->second+edge.a0%4,4*b->second+edge.a1%4,4*a->second+edge.b0%4,4*b->second+edge.b1%4});
        }
        region->checkRegion=std::move(check);
        surface_region_=std::move(region);
    }
    ++surface_region_revision_;
    if(requested_time_idx_>=0) loadFrame_(requested_time_idx_);
}

void SWMM2DResultsLayer::readSurfaceJob_(SurfaceJob& job)
{
    if(job.region) {
        // When a complete full-domain frame is already displayed, use it as an
        // additional absolute-error check before switching this view to a regional fit.
        auto baseline=published_key_;baseline.regionRev=job.key.regionRev;
        if(!published_region_ && baseline==job.key && surfaceDepths_.size()==cells_.size())
            for(int c:job.region->coreCells) job.reference.push_back(surfaceDepths_[size_t(c)]);
    }
    // A live source can acquire geometry before its first hydraulic tick.
    // Validate optional edge arrays even when there was no full-frame bootstrap.
    const size_t edgeValues=4*cells_.size();
    if(!have_edge_geom_ || edge_length_.size()!=edgeValues || edge_nx_.size()!=edgeValues || edge_ny_.size()!=edgeValues) {
        have_edge_geom_=source_->readEdgeGeometry(edge_length_,edge_nx_,edge_ny_)
            && edge_length_.size()==edgeValues && edge_nx_.size()==edgeValues && edge_ny_.size()==edgeValues;
        if(!have_edge_geom_) {edge_length_.clear();edge_nx_.clear();edge_ny_.clear();}
    }
    if(const auto* file=dynamic_cast<const HDF5Mesh2DSource*>(source_.get())) {
        job.inputPath=file->path();return;
    }
    if (!job.region) {readSurfaceInputs_(job.key.frame,job.depths,job.flux);return;}
    const auto frame=source_->readHydraulicCellsAt(job.key.frame,job.region->cells);
    if (frame && frame->time==job.key.frame && frame->generation==job.key.generation
        && frame->revision==job.key.tickRev && frame->cells==job.region->cells
        && frame->depths.size()==job.region->cells.size()) {
        job.depths=frame->depths;
        if(frame->flux.size()==4*job.depths.size()) {job.flux=frame->flux;edge_flux_probe_=1;}
    } else job.depths.assign(job.region->cells.size(),std::numeric_limits<float>::quiet_NaN());
    for(float& d:job.depths) if(!std::isfinite(d) || d<0) d=std::numeric_limits<float>::quiet_NaN();
}

void SWMM2DResultsLayer::publishRegionalSurface_(const SurfaceJob& job)
{
    const float nan=std::numeric_limits<float>::quiet_NaN();
    const CellWaterGeometry::CornerDepths missing{nan,nan,nan,nan};
    // Legacy query/render buffers keep their global indexing. Allocate once;
    // subsequent publications only touch the previous and new regional cells.
    auto clearCell=[&](int c) {
        current_depths_[size_t(c)]=nan;cellSurfaces_[size_t(c)]={};
        surfaceDepths_[size_t(c)]=cellFlowDepths_[size_t(c)]=missing;
        for(int e=0;e<4;++e) current_flux_[4*size_t(c)+e]=0;
        for(int t=cellTri0_[size_t(c)];t<cellTri0_[size_t(c)+1];++t) {
            auto& tri=m_sceneTris[t];tri.depth=tri.dv0=tri.dv1=tri.dv2=nan;
            tri.vx=tri.vy=tri.vmag=0;
        }
    };
    if(!published_region_) {
        current_depths_.resize(cells_.size());current_flux_.resize(4*cells_.size());
        cellSurfaces_.resize(cells_.size());surfaceDepths_.resize(cells_.size());cellFlowDepths_.resize(cells_.size());
        vvx_.assign(vx_.size(),0);vvy_.assign(vx_.size(),0);
        for(int c=0;c<int(cells_.size());++c)clearCell(c);
    } else {
        for(int c:published_region_->cells) clearCell(c);
        for(int v:published_region_->vertices) {vvx_[size_t(v)]=0;vvy_[size_t(v)]=0;}
    }
    const auto& region=*job.region;
    std::unordered_map<int,size_t> vertex;vertex.reserve(region.vertices.size());
    for(size_t v=0;v<region.vertices.size();++v)vertex.emplace(region.vertices[v],v);
    std::vector<float> weight(region.vertices.size(),0);
    float peak=0;
    for(size_t i=0;i<region.cells.size();++i) {
        const size_t c=size_t(region.cells[i]);const auto& ids=cells_[c];const int nv=ids[3]>=0?4:3;
        current_depths_[c]=job.depths[i];cellSurfaces_[c]=surfaceForDepth(int(c),job.depths[i]);
        surfaceDepths_[c]=job.smooth[i];cellFlowDepths_[c]=job.flow.empty()?job.smooth[i]:job.flow[i];
        double vx=0,vy=0;
        if(!job.flux.empty()) {
            std::copy_n(job.flux.data()+4*i,4,current_flux_.data()+4*c);
            if(have_edge_geom_ && CellWaterGeometry::visible(
                CellWaterGeometry::displayState(job.smooth[i],nv,job.policy),job.policy))
                if(!mesh::cellVelocity(int(c),nv,current_flux_.data(),edge_length_.data(),edge_nx_.data(),
                    edge_ny_.data(),job.depths[i],dry_depth_,vx,vy)) vx=vy=0;
        }
        const float speed=float(std::hypot(vx,vy));peak=std::max(peak,speed);
        for(int t=cellTri0_[c];t<cellTri0_[c+1];++t) {
            auto& tri=m_sceneTris[t];tri.depth=job.depths[i];tri.vx=float(vx);tri.vy=float(-vy);tri.vmag=speed;
            float* corners[]={&tri.dv0,&tri.dv1,&tri.dv2};
            for(int k=0;k<3;++k) {
                const int v=tris_[size_t(t)][k];const auto at=std::find(ids.begin(),ids.end(),v);
                *corners[k]=v>=0 && at!=ids.end()?float(job.smooth[i][size_t(at-ids.begin())]):nan;
                if(!max_depth_user_set_ && std::isfinite(*corners[k])) max_depth_=std::max(max_depth_,double(*corners[k]));
            }
        }
        if(speed>0) for(int k=0;k<nv;++k) {
            const int v=ids[k];const float w=job.depths[i]*(nv==4?.75f:1.f);
            vvx_[size_t(v)]+=float(vx)*w;vvy_[size_t(v)]-=float(vy)*w;weight[vertex.at(v)]+=w;
        }
    }
    for(size_t i=0;i<region.vertices.size();++i) if(weight[i]>0) {
        const size_t v=size_t(region.vertices[i]);vvx_[v]/=weight[i];vvy_[v]/=weight[i];
    }
    have_velocity_=peak>0;
    if(!max_velocity_user_set_) max_velocity_=std::max(max_velocity_,double(peak));
    published_region_=job.region;
}

std::shared_ptr<const SWMM2DResultsLayer::SurfaceJob> SWMM2DResultsLayer::analysisSurface_() const
{
    if(!published_region_ || !source_ || current_time_idx_<0) return {};
    auto key=surfaceKey_(current_time_idx_);key.regionRev=0;
    if(analysis_surface_ && analysis_surface_->key==key) return analysis_surface_;
    auto job=std::make_shared<SurfaceJob>();job->key=key;job->policy=waterVisibilityPolicy();
    const_cast<SWMM2DResultsLayer*>(this)->readSurfaceInputs_(key.frame,job->depths,job->flux);
    fitSurface_(*job);analysis_surface_=job;return job;
}
