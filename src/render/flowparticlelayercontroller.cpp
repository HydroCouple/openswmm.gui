// GPL-3.0-or-later
#include "render/flowparticlelayercontroller.h"
#include <QJsonDocument>
#include "layers/swmm2dresultslayer.h"
#include "mesh/meshcellgeom.h"
#include "render/flowparticleresidency.h"
#include <QElapsedTimer>
#include <QLoggingCategory>
#include <algorithm>
#include <cmath>
Q_DECLARE_LOGGING_CATEGORY(lcFlowPerf)
namespace OpenSWMM::Render {
FlowParticleLayerController::FlowParticleLayerController(SWMM2DResultsLayer *layer)
    :QObject(layer),m_layer(layer),m_worker(new FlowParticleController(this)) {
    connect(m_worker,&FlowParticleController::ready,layer,&SWMM2DResultsLayer::flowStreaksChanged);
}
std::array<int,3> FlowParticleLayerController::neighbors(int i) const {
    std::array<int,3> result{-1,-1,-1};
    const auto &tri=m_layer->tris_[size_t(i)];
    const int cell=m_layer->triCell_[size_t(i)];const auto &cv=m_layer->cells_[size_t(cell)];
    const int nv=cv[3]>=0?4:3;
    // Use the already validated global topology. Cropping/sampling must never
    // manufacture a manifold edge by missing a third (possibly dry) face.
    if(m_layer->surfaceNeighbors_.size()!=m_layer->cells_.size()) return result;
    for(int e=0;e<3;++e) {
        const int a=tri[(e+1)%3],b=tri[(e+2)%3];int other=cell;
        for(int k=0;k<nv;++k) if((cv[k]==a && cv[(k+1)%nv]==b) || (cv[k]==b && cv[(k+1)%nv]==a)) {
            other=m_layer->surfaceNeighbors_[size_t(cell)][k];break;
        }
        if(other<0) continue;
        for(int t=m_layer->cellTri0_[size_t(other)];t<m_layer->cellTri0_[size_t(other)+1];++t) if(t!=i) {
            const auto &v=m_layer->tris_[size_t(t)];
            if(std::find(v.begin(),v.end(),a)!=v.end() && std::find(v.begin(),v.end(),b)!=v.end()) {
                result[e]=t;break;
            }
        }
    }
    return result;
}
bool FlowParticleLayerController::overview(const QRectF &view) const {
    if(m_layer->tris_.size()<=32000)return false;
    const auto &grid=m_layer->m_triGrid;
    if(grid.isEmpty())return false;
    const double pad=std::max(view.width(),view.height())*.5;
    const auto coverage=view.adjusted(-pad,-pad,pad,pad).intersected(grid.extent);
    const double estimate=coverage.width()*coverage.height()/(grid.cellW*grid.cellH)*grid.avgPerCell;
    return estimate>32000;
}
bool FlowParticleLayerController::isBackwardSeek() const {
    if(!m_layer->source_)return false;
    const auto time=m_layer->source_->simTimeAt(m_layer->current_time_idx_);
    return m_particleSimTime.isValid() && time.isValid()?time<m_particleSimTime:
        m_particleTime>=0 && m_layer->current_time_idx_<m_particleTime && m_layer->source_->historyGeneration()==m_particleGeneration;
}
bool FlowParticleLayerController::canUsePublishedHydraulics() const {
    if(!m_layer->source_ || !m_layer->source_->isLive() || isBackwardSeek()
       || m_layer->published_region_ || !m_layer->m_edgeFluxAvailable
       || m_layer->current_time_idx_<0 || m_layer->current_time_idx_>=m_layer->source_->timeCount()
       || m_layer->current_depths_.size()!=m_layer->cells_.size()
       || m_layer->current_flux_.size()!=4*m_layer->cells_.size())return false;
    auto current=m_layer->surfaceKey_(m_layer->current_time_idx_);
    // A live packet can replace this time frame before its water fit is
    // displayed. The retained native arrays still belong to the shown tick.
    current.tickRev=m_layer->published_key_.tickRev;
    return current==m_layer->published_key_;
}
std::shared_ptr<const openswmmvis::io::Mesh2DHydraulicFrame>
FlowParticleLayerController::readHydraulicCells(const std::vector<int> &cells) {
    if(canUsePublishedHydraulics() && m_layer->published_key_.tickRev
       !=m_layer->source_->frameContentRevision(m_layer->current_time_idx_)) {
        auto raw=std::make_shared<openswmmvis::io::Mesh2DHydraulicFrame>();
        raw->time=m_layer->published_key_.frame;raw->revision=m_layer->published_key_.tickRev;
        raw->generation=m_layer->published_key_.generation;raw->cells=cells;
        raw->depths.reserve(cells.size());raw->flux.reserve(4*cells.size());
        for(int cell:cells) {
            raw->depths.push_back(m_layer->current_depths_[size_t(cell)]);
            const auto begin=m_layer->current_flux_.begin()+4*size_t(cell);
            raw->flux.insert(raw->flux.end(),begin,begin+4);
        }
        return raw;
    }
    ++m_particleReads;
    return m_layer->source_->readHydraulicCellsAt(m_layer->current_time_idx_,cells);
}
FlowParticleResidencyGrid FlowParticleLayerController::residencyGrid(bool sparse) const {
    FlowParticleResidencyGrid grid(m_layer->m_triGrid);
    if(sparse) {
        grid.cellW/=8;grid.cellH/=8;grid.cols*=8;grid.rows*=8;grid.maxTiles=65536;
    }
    return grid;
}
int FlowParticleLayerController::overviewCapacity(double scale,double trail,double speed) const {
    const auto &grid=m_layer->m_triGrid;
    const double density=double(grid.avgPerCell)/(grid.cellW*grid.cellH);
    const double width=std::max(grid.cellW,grid.cellH)/6;
    const double distance=(4.5+trail)*48*std::max(.25,speed)/scale;
    const double perParticle=std::max(1.,density*width*distance);
    return std::clamp(int(std::min(16.,24000/perParticle)),1,16);
}
bool FlowParticleLayerController::prepareOverviewSeeds(const QRectF &view,double scale,int capacity) {
    const double speed=m_layer->flowStreakSublayer()->streakStyle()->speedMultiplier();
    const double cutoff=m_layer->showThinFilms()?0:m_layer->thinFilmDepth();
    // Rendering adds a two-pixel culling margin and can round its scale to
    // float. Capture/observation uses the exact viewport. Reusing these seed
    // patches avoids alternating native-field reloads for the same map view.
    const double tolerance=4/scale;
    const bool sameView=m_overviewView.isValid()
        && std::abs(m_overviewView.left()-view.left())<=tolerance
        && std::abs(m_overviewView.right()-view.right())<=tolerance
        && std::abs(m_overviewView.top()-view.top())<=tolerance
        && std::abs(m_overviewView.bottom()-view.bottom())<=tolerance;
    if(sameView && m_overviewRevision==m_layer->frame_revision_ && std::abs(m_overviewScale-scale)<=1e-6*scale
       && m_overviewSpeed==speed && m_overviewCutoff==cutoff && m_overviewCapacity==capacity)return true;
    struct Probe {int triangle=-1,cell=-1;QPointF point;double area=0;};
    std::vector<Probe> probes;probes.reserve(1024);
    const auto cross=[](QPointF a,QPointF b){return a.x()*b.y()-a.y()*b.x();};
    const auto &grid=m_layer->m_triGrid;
    // One deterministic jittered probe per stratum. Point location uses only
    // the existing immutable index; no all-domain field or locator is copied.
    for(int y=0;y<32;++y)for(int x=0;x<32;++x) {
        const quint64 hash=quint64(y*32+x+1)*0x9e3779b97f4a7c15ULL;
        const double jx=.1+.8*double((hash>>16)&65535)/65535.;
        const double jy=.1+.8*double((hash>>40)&65535)/65535.;
        const QPointF point(view.left()+(x+jx)*view.width()/32,view.top()+(y+jy)*view.height()/32);
        const int *begin=nullptr,*end=nullptr;grid.candidatesAtPoint(point.x(),point.y(),begin,end);
        for(auto it=begin;it!=end;++it) {
            const auto &tri=m_layer->tris_[size_t(*it)];const auto a=m_layer->m_sceneVerts[tri[0]];
            const auto ab=m_layer->m_sceneVerts[tri[1]]-a,ac=m_layer->m_sceneVerts[tri[2]]-a;
            const double det=cross(ab,ac);if(std::abs(det)<1e-20)continue;
            const double b=cross(point-a,ac)/det,c=cross(ab,point-a)/det;
            if(b>=-1e-9 && c>=-1e-9 && b+c<=1+1e-9) {
                probes.push_back({*it,m_layer->triCell_[size_t(*it)],point,std::abs(det)*.5});break;
            }
        }
    }
    if(probes.empty())return false;
    std::vector<int> cells;cells.reserve(probes.size());
    for(const auto &probe:probes)cells.push_back(probe.cell);
    std::sort(cells.begin(),cells.end());cells.erase(std::unique(cells.begin(),cells.end()),cells.end());
    const auto raw=readHydraulicCells(cells);
    if(!raw || raw->time!=m_layer->current_time_idx_ || raw->revision!=m_layer->published_key_.tickRev
       || raw->generation!=m_layer->published_key_.generation || raw->cells!=cells || raw->depths.size()!=cells.size())return false;
    std::vector<float> vx,vy;
    if(raw->flux.empty()) {
        if(!m_layer->source_->readFaceFieldCellsAt("Mesh2_face_vx",m_layer->current_time_idx_,cells,vx)
           || !m_layer->source_->readFaceFieldCellsAt("Mesh2_face_vy",m_layer->current_time_idx_,cells,vy)
           || vx.size()!=cells.size() || vy.size()!=cells.size())return false;
    } else if(raw->flux.size()!=4*cells.size())return false;
    std::vector<OverviewSeed> wet;wet.reserve(probes.size());
    std::vector<double> speeds;
    for(const auto &probe:probes) {
        const size_t local=size_t(std::lower_bound(cells.begin(),cells.end(),probe.cell)-cells.begin());
        const double depth=raw->depths[local];if(!std::isfinite(depth) || depth<=0)continue;
        const auto surface=CellWaterGeometry::reconstruct(m_layer->cellSplit_[size_t(probe.cell)],depth,m_layer->vz_);
        const auto &tri=m_layer->tris_[size_t(probe.triangle)];
        const QPointF a=m_layer->m_sceneVerts[tri[0]],ab=m_layer->m_sceneVerts[tri[1]]-a,ac=m_layer->m_sceneVerts[tri[2]]-a;
        const double det=cross(ab,ac),b=cross(probe.point-a,ac)/det,c=cross(ab,probe.point-a)/det;
        const double bed=m_layer->vz_[size_t(tri[0])]*(1-b-c)+m_layer->vz_[size_t(tri[1])]*b+m_layer->vz_[size_t(tri[2])]*c;
        if(surface.state==CellWaterGeometry::State::Dry || surface.signedDepth(bed)<=cutoff)continue;
        QPointF velocity;
        if(!vx.empty())velocity={vx[local],vy[local]};
        else {
            // This least-squares direction is used only for prefetch. The
            // integrator still reconstructs the exact native RT0 face field.
            double xx=0,xy=0,yy=0,bx=0,by=0;
            const auto &cell=m_layer->cells_[size_t(probe.cell)];const int n=cell[3]>=0?4:3;
            for(int e=0;e<n;++e) {
                const size_t slot=size_t(probe.cell)*4+e;
                if(slot>=m_layer->edge_nx_.size() || slot>=m_layer->edge_ny_.size() || slot>=m_layer->edge_length_.size())return false;
                const double nx=m_layer->edge_nx_[slot],ny=m_layer->edge_ny_[slot],length=m_layer->edge_length_[slot];
                if(length<=0)continue;
                const double normal=raw->flux[local*4+e]/(depth*length);
                xx+=nx*nx;xy+=nx*ny;yy+=ny*ny;bx+=nx*normal;by+=ny*normal;
            }
            const double determinant=xx*yy-xy*xy;
            if(determinant>1e-20)velocity={(bx*yy-by*xy)/determinant,(by*xx-bx*xy)/determinant};
        }
        const QPointF nativeAB(m_layer->vx_[size_t(tri[1])]-m_layer->vx_[size_t(tri[0])],m_layer->vy_[size_t(tri[1])]-m_layer->vy_[size_t(tri[0])]);
        const QPointF nativeAC(m_layer->vx_[size_t(tri[2])]-m_layer->vx_[size_t(tri[0])],m_layer->vy_[size_t(tri[2])]-m_layer->vy_[size_t(tri[0])]);
        const double nativeDet=cross(nativeAB,nativeAC);
        if(std::abs(nativeDet)>1e-20)velocity=ab*(cross(velocity,nativeAC)/nativeDet)+ac*(cross(nativeAB,velocity)/nativeDet);
        const double magnitude=std::hypot(velocity.x(),velocity.y());
        if(!std::isfinite(magnitude))continue;
        std::array<double,3> cornerDepth;
        for(int k=0;k<3;++k)cornerDepth[k]=surface.signedDepth(m_layer->vz_[size_t(tri[k])])-cutoff;
        std::sort(cornerDepth.begin(),cornerDepth.end());
        double wetArea=probe.area;
        if(cornerDepth[0]<0) {
            if(cornerDepth[1]<=0)wetArea*=cornerDepth[2]/(cornerDepth[2]-cornerDepth[0])
                *cornerDepth[2]/(cornerDepth[2]-cornerDepth[1]);
            else wetArea*=1-cornerDepth[0]*cornerDepth[0]/((cornerDepth[1]-cornerDepth[0])*(cornerDepth[2]-cornerDepth[0]));
        }
        wet.push_back({probe.triangle,wetArea,a+(ab+ac)/3.,velocity});speeds.push_back(magnitude);
    }
    m_overviewSeeds.clear();
    if(wet.empty())m_overviewSeeds.push_back({probes.front().triangle,0,probes.front().point,{}});
    else {
        std::sort(speeds.begin(),speeds.end());const double reference=speeds[size_t(.9*(speeds.size()-1))];
        const int count=std::min(capacity,int(wet.size()));
        const double representedArea=view.width()*view.height()*wet.size()/1024./count;
        for(int i=0;i<count;++i) {
            const size_t begin=size_t(i)*wet.size()/count,end=size_t(i+1)*wet.size()/count;
            quint64 bits=quint64(i+1)*0xd1b54a32d192ed03ULL;
            bits=(bits^(bits>>30))*0xbf58476d1ce4e5b9ULL;bits=(bits^(bits>>27))*0x94d049bb133111ebULL;
            const double fraction=double((bits^(bits>>31))>>11)/9007199254740992.;
            auto seed=wet[begin+size_t(fraction*(end-begin))];
            const double magnitude=std::hypot(seed.velocity.x(),seed.velocity.y());
            if(reference>0 && magnitude>0)seed.velocity*=48*speed/scale*std::max(1.,std::cbrt(magnitude/reference))/magnitude;
            seed.weight=seed.weight>0?representedArea/seed.weight:0;
            m_overviewSeeds.push_back(seed);
        }
    }
    m_overviewView=view;m_overviewRevision=m_layer->frame_revision_;m_overviewScale=scale;m_overviewSpeed=speed;
    m_overviewCutoff=cutoff;m_overviewCapacity=capacity;
    ++m_overviewLayout;
    return true;
}
FlowParticleResidency FlowParticleLayerController::seedResidency(const FlowParticleResidencyGrid &grid) const {
    FlowParticleFrame seeds;
    for(const auto &seed:m_overviewSeeds) {
        FlowParticle particle;particle.position=seed.position;
        particle.history.push_back({seed.position-seed.velocity,0,-1});
        particle.history.push_back({seed.position,1,-1});particle.visibleAfterAge=0;
        seeds.particles.push_back(std::move(particle));
    }
    return flowParticleResidency(grid.extent,grid,&seeds,2.5,false);
}
void FlowParticleLayerController::advance(double seconds) {
    const auto *style=m_layer->flowStreakSublayer()->streakStyle();
    if(!m_layer->flowStreaksEnabled() || !style->animated() || style->speedMultiplier()<=0
       || !std::isfinite(seconds) || seconds<=0)return;
    if(m_particleView.isValid())frame(m_particleView,m_particleScale,m_layer->frameRevision(),false);
    m_worker->advance(seconds);
}
std::shared_ptr<const FlowParticleNativeInput> FlowParticleLayerController::capture(
    const QRectF &view,double scale)
{
    if(!m_layer->source_ || !view.isValid() || !std::isfinite(scale) || scale<=0
       || (m_layer->source_->isLive() && !m_layer->liveRenderEnabled()))return {};
    const auto visible=view.intersected(m_layer->m_sceneBBox);if(!visible.isValid())return {};
    const quint64 epoch=m_layer->geomRevision()*0x9e3779b97f4a7c15ULL+m_layer->source_revision_;
    if(epoch!=m_particleEpoch) {
        m_particleMesh.reset();m_particleTiles.clear();m_overviewSeeds.clear();m_overviewRevision=~quint64(0);
        m_particleEpoch=epoch;m_particleRevision=~quint64(0);m_particleSimTime={};m_particleTime=-1;m_particleCheckClock=-1;
        m_particleResetRevision=~quint64(0);
    }
    const auto frame=m_worker->latestFrame();
    const auto support=m_worker->residency();
    const bool seek=isBackwardSeek();
    const bool resetting=seek || (m_particleResetRevision==m_layer->frame_revision_
        && (!frame || frame->geometry!=epoch || frame->revision!=m_particleResetRevision));
    const double clock=support && support->geometry==epoch && support->revision==m_layer->frame_revision_?support->clock:
        frame && frame->geometry==epoch && frame->revision==m_layer->frame_revision_?frame->clock:0;
    const double cutoff=m_layer->showThinFilms()?0:m_layer->thinFilmDepth();
    const bool sparse=overview(visible);
    const auto *style=m_layer->flowStreakSublayer()->streakStyle();
    const int capacity=sparse?overviewCapacity(scale,style->trailDurationSeconds(),style->speedMultiplier()):0;
    // Seed the visible water and a bounded motion margin, rather than a
    // half-viewport at every zoom. Exact head/trail support is merged below.
    const double padding=sparse?0:std::min(std::max(128.,96*style->speedMultiplier())/scale,
        std::max(m_layer->m_sceneBBox.width(),m_layer->m_sceneBBox.height()));
    const auto checkedView=visible.adjusted(-padding,-padding,padding,padding);
    // The two-second head margin permits four support checks per animation
    // second. View/result changes bypass this throttle immediately.
    if(!seek && m_particleInput && m_particleInput->geometry==epoch && m_particleInput->revision==m_layer->frame_revision_ && m_particleInput->cutoff==cutoff
        && checkedView==m_particleCheckedView && m_particleOverview==sparse
        && (!sparse || (m_overviewScale==scale && m_overviewSpeed==style->speedMultiplier() && m_overviewCapacity==capacity))
        && clock>=m_particleCheckClock && clock-m_particleCheckClock<.25) {
        m_particleLimit.clear();return m_particleInput;
    }
    const bool modeChanged=m_particleOverview!=sparse;
    const auto grid=residencyGrid(sparse);
    const auto coherent=[&] {return (m_layer->published_key_.frame==m_layer->current_time_idx_
        && m_layer->published_key_.tickRev==m_layer->source_->frameContentRevision(m_layer->current_time_idx_)
        && m_layer->published_key_.generation==m_layer->source_->historyGeneration()) || canUsePublishedHydraulics();};
    if(!coherent())return {};
    if(sparse && (!prepareOverviewSeeds(visible,scale,capacity) || !coherent()))return {};
    const auto base=sparse?seedResidency(grid):flowParticleResidency(visible,grid,nullptr,2,true,padding);
    const bool matchingSupport=support && support->geometry==epoch && support->grid.cellW==grid.cellW
        && support->grid.cellH==grid.cellH && support->grid.extent==grid.extent;
    auto residency=base;
    if(!resetting) {
        if(matchingSupport)mergeFlowParticleResidency(residency,support->required,grid);
        else if(frame && frame->geometry==epoch)mergeFlowParticleResidency(residency,flowParticleResidency(visible,grid,frame.get(),.5,false),grid);
    }
    if(residency.limited) {
        m_particleLimit=tr("Zoom in to render continuous particles within the regional field memory limit.");return {};
    }
    const bool contains=!seek && !modeChanged && (!sparse || m_particleSeedLayout==m_overviewLayout)
        && std::includes(m_particleTiles.begin(),m_particleTiles.end(),residency.tiles.begin(),residency.tiles.end());
    const bool prune=m_particleTiles.size()>2*residency.tiles.size() && clock-m_particleCaptureClock>2;
    if(contains && !prune && m_particleInput && m_particleInput->geometry==epoch && m_particleInput->revision==m_layer->frame_revision_
        && m_particleInput->cutoff==cutoff) {
        m_particleCheckClock=clock;m_particleCheckedView=checkedView;m_particleLimit.clear();return m_particleInput;
    }
    if(!contains || prune) {
        // Required support looks half a second ahead; captures load two seconds.
        // Requiring the entire prefetch margin would replace the mesh each check.
        residency=base;
        if(!resetting) {
            if(matchingSupport)mergeFlowParticleResidency(residency,support->prefetch,grid);
            else if(frame && frame->geometry==epoch)mergeFlowParticleResidency(residency,flowParticleResidency(visible,grid,frame.get(),2,false),grid);
        }
        if(residency.limited) {
            m_particleLimit=tr("Zoom in to render continuous particles within the regional field memory limit.");return {};
        }
    }
    if(!coherent())return {};
    QElapsedTimer captureTimer;captureTimer.start();
    auto mesh=contains && !prune?m_particleMesh:
        FlowParticleMesh::captureRegions(m_layer->m_sceneVerts,m_layer->tris_,m_layer->m_triGrid,residency.rectangles);
    if(!mesh) {
        m_particleLimit=tr("Zoom in to render continuous particles: the regional geometry limit is 250,000 display triangles.");return {};
    }
    const bool newMesh=mesh!=m_particleMesh;
    if(sparse && mesh!=m_particleMesh) {
        mesh->seedWeights.assign(mesh->triangles.size(),0);
        for(const auto &seed:m_overviewSeeds) {
            const auto it=std::lower_bound(mesh->sourceTriangles.begin(),mesh->sourceTriangles.end(),seed.triangle);
            if(it!=mesh->sourceTriangles.end() && *it==seed.triangle)
                mesh->seedWeights[size_t(it-mesh->sourceTriangles.begin())]+=seed.weight;
        }
    }
    const size_t count=mesh->triangles.size();
    const auto meshBytes=[](const FlowParticleMesh &m) {return m.bytes()+m.triangles.size()*sizeof(std::array<int,3>);};
    // Bound old, pending and replacement meshes/inputs, the temporal blend,
    // immutable fields, adjacency/discharge scratch and sparse capture scratch.
    // Keep the high-water dimensions while older jobs/transition fields may
    // still own a different layout. Coalescing must not hide a larger old field.
    const size_t peakCount=std::max(count,m_particlePeakTriangles);
    const size_t peakMesh=std::max(meshBytes(*mesh),m_particlePeakMeshBytes);
    const size_t peak=3*peakMesh+peakCount*(3*sizeof(FlowParticleNativeTriangle)
        +5*sizeof(FlowParticleField)+1024)+8*1024*1024;
    if(peak>128*1024*1024) {
        m_particleLimit=tr("Zoom in to render continuous particles within the regional field memory limit.");return {};
    }
    const bool reuse=m_particleInput && m_particleInput->geometry==epoch && m_particleInput->revision==m_layer->frame_revision_;
    std::vector<int> velocityCells;
    std::vector<float> velocityX,velocityY;
    if(!m_layer->m_edgeFluxAvailable && m_layer->source_->hasFaceField("Mesh2_face_vx") && m_layer->source_->hasFaceField("Mesh2_face_vy")) {
        for(int id:mesh->sourceTriangles) {
            const int cell=m_layer->triCell_[size_t(id)];
            if(velocityCells.empty() || velocityCells.back()!=cell)velocityCells.push_back(cell);
        }
        if(!m_layer->source_->readFaceFieldCellsAt("Mesh2_face_vx",m_layer->current_time_idx_,velocityCells,velocityX)
           || !m_layer->source_->readFaceFieldCellsAt("Mesh2_face_vy",m_layer->current_time_idx_,velocityCells,velocityY)
           || velocityX.size()!=velocityCells.size() || velocityY.size()!=velocityCells.size() || !coherent())return {};
    }
    std::vector<int> missing;
    for(int id:mesh->sourceTriangles) {
        if(reuse && std::binary_search(m_particleInput->mesh->sourceTriangles.begin(),m_particleInput->mesh->sourceTriangles.end(),id))continue;
        const int cell=m_layer->triCell_[size_t(id)];
        if(!std::isfinite(m_layer->current_depths_[size_t(cell)]) && (missing.empty() || missing.back()!=cell))missing.push_back(cell);
    }
    std::shared_ptr<const openswmmvis::io::Mesh2DHydraulicFrame> raw;
    if(!missing.empty() && m_layer->source_->supportsHydraulicCells()) {
        raw=readHydraulicCells(missing);
        if(!raw || raw->time!=m_layer->current_time_idx_ || raw->revision!=m_layer->published_key_.tickRev
            || raw->generation!=m_layer->published_key_.generation || raw->cells!=missing
            || raw->depths.size()!=missing.size() || !coherent())return {};
    }
    auto input=std::make_shared<FlowParticleNativeInput>();input->mesh=mesh;
    input->geometry=epoch;input->revision=m_layer->frame_revision_;input->cutoff=cutoff;input->triangles.reserve(count);
    for(int id:mesh->sourceTriangles) {
        FlowParticleNativeTriangle t;
        const auto &vi=m_layer->tris_[size_t(id)];const int cell=m_layer->triCell_[size_t(id)];
        const auto &native=m_layer->cells_[size_t(cell)];const int nv=native[3]>=0?4:3;
        const auto cached=reuse?std::lower_bound(m_particleInput->mesh->sourceTriangles.begin(),m_particleInput->mesh->sourceTriangles.end(),id):std::vector<int>::const_iterator{};
        if(reuse && cached!=m_particleInput->mesh->sourceTriangles.end() && *cached==id) {
            t=m_particleInput->triangles[size_t(cached-m_particleInput->mesh->sourceTriangles.begin())];
        } else {
            const auto &display=m_layer->m_sceneTris[id];t.depth={display.dv0,display.dv1,display.dv2};
            t.meanDepth=m_layer->current_depths_[size_t(cell)];
            const float *flux=m_layer->m_edgeFluxAvailable && m_layer->current_flux_.size()==4*m_layer->cells_.size()?m_layer->current_flux_.data()+4*cell:nullptr;
            if(!std::isfinite(t.meanDepth) && raw) {
                const auto it=std::lower_bound(raw->cells.begin(),raw->cells.end(),cell);
                if(it!=raw->cells.end() && *it==cell) {
                    const size_t local=size_t(it-raw->cells.begin());t.meanDepth=raw->depths[local];
                    flux=raw->flux.size()==4*raw->cells.size()?raw->flux.data()+4*local:nullptr;
                    const auto surface=CellWaterGeometry::reconstruct(m_layer->cellSplit_[size_t(cell)],t.meanDepth,m_layer->vz_);
                    for(int k=0;k<3;++k)t.depth[k]=surface.state==CellWaterGeometry::State::Dry?0:
                        float(surface.signedDepth(m_layer->vz_[size_t(vi[k])]));
                }
            }
            t.available=flux && std::isfinite(t.meanDepth);
            for(int k=0;k<3;++k)t.metres[k]={m_layer->vx_[size_t(vi[k])],m_layer->vy_[size_t(vi[k])]};
            double external=0,total=0;int diagonal=-1;
            for(int k=0;k<nv;++k)total+=t.available?flux[k]:NAN;
            for(int e=0;e<3;++e) {
                const int a=vi[(e+1)%3],b=vi[(e+2)%3];int slot=-1;
                for(int k=0;k<nv;++k) {
                    const int x=native[(k+1)%nv],y=native[(k+2)%nv];
                    if((x==a && y==b)||(x==b && y==a))slot=k;
                }
                if(slot<0)diagonal=e;
                else {t.flux[e]=t.available?flux[slot]:NAN;external+=t.flux[e];}
            }
            if(diagonal>=0) {
                const auto &split=m_layer->cellSplit_[size_t(cell)];const int sub=id-m_layer->cellTri0_[size_t(cell)];
                const double area=split.area[0]+split.area[1];
                t.flux[diagonal]=area>0?total*split.area[size_t(sub)]/area-external:NAN;
            }
            if(!flux && !velocityX.empty()) {
                const auto it=std::lower_bound(velocityCells.begin(),velocityCells.end(),cell);
                const size_t local=size_t(it-velocityCells.begin());
                t.velocitySI={velocityX[local],velocityY[local]};t.velocityOnly=true;
                t.available=std::isfinite(t.meanDepth) && std::isfinite(t.velocitySI.x()) && std::isfinite(t.velocitySI.y());
            }
        }
        t.residentEdges=7;const auto topology=neighbors(id);
        for(int e=0;e<3;++e)if(topology[e]>=0
            && !std::binary_search(mesh->sourceTriangles.begin(),mesh->sourceTriangles.end(),topology[e]))
                t.residentEdges&=~(1<<e);
        input->triangles.push_back(t);
    }
    if(!coherent())return {};
    m_particleMesh=mesh;m_particleInput=input;
    m_particleOverview=sparse;
    m_particleSeedLayout=m_overviewLayout;
    if(!contains || prune)m_particleTiles=residency.tiles;
    m_particleCaptureClock=clock;m_particleCheckClock=clock;m_particleCheckedView=checkedView;m_particleFieldBytes=peak;
    m_particlePeakTriangles=peakCount;m_particlePeakMeshBytes=peakMesh;++m_particleCaptures;m_particleLimit.clear();
    m_lastCaptureMs=captureTimer.nsecsElapsed()/1e6;m_captureMsTotal+=m_lastCaptureMs;
    qCDebug(lcFlowPerf)<<"flow capture"<<(newMesh?"new mesh":"reused mesh")<<count<<"triangles"<<residency.tiles.size()<<"tiles"
        <<(sparse?"overview":"regional")<<m_lastCaptureMs<<"ms";
    return input;
}

bool FlowParticleLayerController::prepareForPresentation(std::shared_ptr<const FlowParticleNativeInput> input) {
    const auto epoch=m_layer->geomRevision()*0x9e3779b97f4a7c15ULL+m_layer->source_revision_;
    if(!input || input->geometry!=epoch)return true;
    m_worker->prepareForPresentation(input);
    const auto prepared=m_worker->frame(input->revision);
    return prepared && prepared->geometry==epoch;
}
std::shared_ptr<const FlowParticleFrame> FlowParticleLayerController::frame(
    const QRectF &view,double scale,quint64 displayedRevision,bool presented)
{
    const auto *style=m_layer->m_flowStreakSublayer->streakStyle();
    if(!m_layer->flowStreaksEnabled() || !view.isValid()
       || !std::isfinite(scale) || scale<=0)return {};
    m_particleView=view;m_particleScale=scale;
    FlowParticleSettings settings;settings.publicationInterval=1./30.;settings.speed=style->speedMultiplier();
    settings.minimumLifetime=style->minimumLifetimeSeconds();settings.maximumLifetime=style->maximumLifetimeSeconds();
    settings.trailSeconds=style->trailDurationSeconds();settings.fadeSeconds=style->lifetimeFadeSeconds();
    settings.cutoff=m_layer->showThinFilms()?0:m_layer->thinFilmDepth();
    settings.historyTolerance=.25/scale;
    // Clamp before conversion: user-selected durations can exceed int range.
    // Preserve the ordinary trail quality. Extra-dense styles use the
    // duration-derived capacity to fit more heads into the same memory budget.
    const double minimumHistory=style->spacingPx()<28?64:512;
    settings.maxHistory=int(std::clamp(std::ceil(settings.trailSeconds*120)+64,minimumHistory,4096.));
    const auto current=m_worker->latestFrame();
    const auto epoch=m_layer->geomRevision()*0x9e3779b97f4a7c15ULL+m_layer->source_revision_;
    const bool oldGeneration=isBackwardSeek() || (current && current->geometry!=epoch)
        || (m_particleResetRevision==m_layer->frame_revision_ && (!current || current->revision!=m_particleResetRevision));
    const size_t currentPopulation=current && !oldGeneration?current->particles.size():0;
    const auto particleBytes=[](int history) {
        // Working/recycled paged histories, four immutable headers, worker
        // packets + renderer/fallback/GPU vertices, timing and static indices.
        return 2*FlowParticleRing<FlowParticleSample>::reservedBytes(size_t(history)+1)
            +4*sizeof(FlowParticle)+sizeof(FlowParticleState)+14*size_t(history)*sizeof(FlowParticleVertex)
            +6*size_t(history-1)*sizeof(quint32)+5*sizeof(FlowParticleTiming)+4*sizeof(quint64);
    };
    constexpr size_t particleBudget=96*1024*1024;
    if(currentPopulation) {
        int low=3,high=settings.maxHistory;
        while(low<high) {
            const int middle=low+(high-low+1)/2;
            if(particleBytes(middle)*currentPopulation<=particleBudget)low=middle;
            else high=middle-1;
        }
        settings.maxHistory=low;
    }
    // Bound the pool and its retained/publication copies before long tails
    // allocate. Lower density rather than shortening assigned particle lives.
    settings.maxParticles=std::max(1,std::min(4000,int(particleBudget/particleBytes(settings.maxHistory))));
    const auto visible=view.intersected(m_layer->m_sceneBBox);
    FlowParticleAppearance appearance;appearance.anchor=m_layer->m_sceneBBox.center();appearance.color=style->color();
    const double stroke=2*std::max(style->widthPx(),style->headWidthPx())/scale;
    appearance.viewport=view.adjusted(-stroke,-stroke,stroke,stroke);
    appearance.revision=qHash(QJsonDocument(style->toJson()).toJson(QJsonDocument::Compact));
    if(appearance.revision!=m_paletteRevision) {
        m_paletteRevision=appearance.revision;m_palette={};
        if(style->colorByMagnitude()) {
            const auto scheme=style->scheme();
            const auto edges=scheme.levelEdges(scheme.rangeMin(),scheme.rangeMax());
            if(scheme.mode()==ClassificationScheme::ClassMode::Classified && edges.size()>1) {
                QVector<QColor> colors;colors.reserve(edges.size()-1);
                for(int i=0;i<edges.size()-1;++i)colors.push_back(scheme.colorForClass(i,edges.size()-1));
                m_palette=[edges,colors](double speed) {
                    const int i=ClassificationScheme::classIndexFor(speed,edges);
                    return i<0?QColor(Qt::transparent):colors[std::min(i,int(colors.size()-1))];
                };
            } else {
                const auto ramp=scheme.resolvedRamp();const bool invert=scheme.invertRamp();
                const auto range=scheme.effectiveRange(scheme.rangeMin(),scheme.rangeMax());
                m_palette=[ramp,invert,range](double speed) {
                    if(!std::isfinite(speed) || speed<range.first)return QColor(Qt::transparent);
                    const double f=range.second>range.first?std::clamp((speed-range.first)/(range.second-range.first),0.,1.):0;
                    return ramp.colorAt(invert?1-f:f);
                };
            }
        }
    }
    appearance.colorForSpeed=m_palette;
    const bool sparse=overview(visible);
    // Leave room for retiring tails and replacement heads at dense settings.
    int population=std::max(1,settings.maxParticles-std::max(1,settings.maxParticles/8));
    if(sparse) {
        population=std::min(population,overviewCapacity(scale,settings.trailSeconds,settings.speed));
        // Sparse heads can reach a nearby shoreline long before their assigned
        // lifetime. Reserve bounded fade slots without increasing seed density.
        settings.maxParticles=std::min(settings.maxParticles,population*16+16);
    }
    FlowParticleView particleView{scale,style->spacingPx(),48,m_layer->m_velocityFloorSI};particleView.animated=style->animated();
    const auto input=displayedRevision==m_layer->frame_revision_?capture(view,scale):nullptr;
    // A seek's replacement limits belong to its replacement field. Applying
    // them while that packet is missing expands abandoned dense histories.
    if(oldGeneration && !input) {
        if(presented)m_worker->setPresentedRevision(displayedRevision);
        return m_worker->frame(displayedRevision);
    }
    m_worker->configure(settings,visible,population,particleView,
                        std::move(appearance),residencyGrid(sparse),false);
    if(input && (m_particleRevision!=input->revision || m_particleCutoff!=settings.cutoff
        || m_worker->requestedMesh()!=input->mesh)) {
        const auto time=m_layer->source_->simTimeAt(m_layer->current_time_idx_);
        const bool reset=isBackwardSeek() || (current && current->geometry!=epoch);
        if(reset)m_particleResetRevision=input->revision;
        m_worker->request(input,reset);m_particleRevision=m_layer->frame_revision_;m_particleTime=m_layer->current_time_idx_;
        m_particleSimTime=time;m_particleGeneration=m_layer->published_key_.generation;m_particleCutoff=settings.cutoff;
    }
    if(presented)m_worker->setPresentedRevision(displayedRevision);
    m_worker->request({});
    return m_worker->frame(displayedRevision);
}
} // namespace OpenSWMM::Render
