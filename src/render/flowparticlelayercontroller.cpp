// GPL-3.0-or-later
#include "render/flowparticlelayercontroller.h"
#include <QJsonDocument>
#include "layers/swmm2dresultslayer.h"
#include "mesh/meshcellgeom.h"
#include "render/flowparticleresidency.h"
#include <algorithm>
#include <cmath>
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
        m_particleMesh.reset();m_particleTiles.clear();
        m_particleEpoch=epoch;m_particleRevision=~quint64(0);m_particleSimTime={};m_particleTime=-1;m_particleCheckClock=-1;
    }
    const auto frame=m_worker->latestFrame();
    const double clock=frame?frame->clock:0;
    const double cutoff=m_layer->showThinFilms()?0:m_layer->thinFilmDepth();
    // The two-second head margin permits four support checks per animation
    // second. View/result changes bypass this throttle immediately.
    if(m_particleInput && m_particleInput->geometry==epoch && m_particleInput->revision==m_layer->frame_revision_ && m_particleInput->cutoff==cutoff
        && visible==m_particleCheckedView && clock>=m_particleCheckClock && clock-m_particleCheckClock<.25) {
        m_particleLimit.clear();return m_particleInput;
    }
    const FlowParticleResidencyGrid grid(m_layer->m_triGrid);
    const auto support=m_worker->residency();
    auto residency=flowParticleResidency(visible,grid,nullptr);
    if(support && support->geometry==epoch)mergeFlowParticleResidency(residency,support->required,grid);
    if(residency.limited) {
        m_particleLimit=tr("Zoom in to render continuous particles within the regional field memory limit.");return {};
    }
    const bool contains=std::includes(m_particleTiles.begin(),m_particleTiles.end(),residency.tiles.begin(),residency.tiles.end());
    const bool prune=m_particleTiles.size()>2*residency.tiles.size() && clock-m_particleCaptureClock>2;
    if(contains && !prune && m_particleInput && m_particleInput->geometry==epoch && m_particleInput->revision==m_layer->frame_revision_
        && m_particleInput->cutoff==cutoff) {
        m_particleCheckClock=clock;m_particleCheckedView=visible;m_particleLimit.clear();return m_particleInput;
    }
    if(!contains || prune) {
        // Required support looks half a second ahead; captures load two seconds.
        // Requiring the entire prefetch margin would replace the mesh each check.
        residency=flowParticleResidency(visible,grid,nullptr);
        if(support && support->geometry==epoch)mergeFlowParticleResidency(residency,support->prefetch,grid);
        if(residency.limited) {
            m_particleLimit=tr("Zoom in to render continuous particles within the regional field memory limit.");return {};
        }
    }
    const auto coherent=[&] {return m_layer->published_key_.frame==m_layer->current_time_idx_
        && m_layer->published_key_.tickRev==m_layer->source_->frameContentRevision(m_layer->current_time_idx_)
        && m_layer->published_key_.generation==m_layer->source_->historyGeneration();};
    if(!coherent())return {};
    auto mesh=contains && !prune?m_particleMesh:
        FlowParticleMesh::captureRegions(m_layer->m_sceneVerts,m_layer->tris_,m_layer->m_triGrid,residency.rectangles);
    if(!mesh) {
        m_particleLimit=tr("Zoom in to render continuous particles: the regional geometry limit is 250,000 display triangles.");return {};
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
        raw=m_layer->source_->readHydraulicCellsAt(m_layer->current_time_idx_,missing);++m_particleReads;
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
    if(!contains || prune)m_particleTiles=residency.tiles;
    m_particleCaptureClock=clock;m_particleCheckClock=clock;m_particleCheckedView=visible;m_particleFieldBytes=peak;
    m_particlePeakTriangles=peakCount;m_particlePeakMeshBytes=peakMesh;++m_particleCaptures;m_particleLimit.clear();
    return input;
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
    settings.maxHistory=int(std::clamp(std::ceil(settings.trailSeconds*120)+64,512.,4096.));
    const auto current=m_worker->latestFrame();
    const size_t currentPopulation=current?current->particles.size():0;
    const auto particleBytes=[](int history) {
        // Working/recycled paged histories, four immutable headers, worker
        // packets + renderer/fallback/GPU vertices, timing and static indices.
        return 2*FlowParticleRing<FlowParticleSample>::reservedBytes(size_t(history)+1)
            +4*sizeof(FlowParticle)+sizeof(FlowParticleState)+14*size_t(history)*sizeof(FlowParticleVertex)
            +6*size_t(history-1)*sizeof(quint32)+5*sizeof(FlowParticleTiming);
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
    settings.maxParticles=std::max(1,std::min(2000,int(particleBudget/particleBytes(settings.maxHistory))));
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
    FlowParticleView particleView{scale,style->spacingPx(),48,m_layer->m_velocityFloorSI};particleView.animated=style->animated();
    m_worker->configure(settings,visible,settings.maxParticles,particleView,
                        std::move(appearance),m_layer->m_triGrid);
    if(presented)m_worker->setPresentedRevision(displayedRevision);
    if(displayedRevision==m_layer->frame_revision_) {
        const auto input=capture(view,scale);
        if(input && (m_particleRevision!=input->revision || m_particleCutoff!=settings.cutoff
            || m_worker->requestedMesh()!=input->mesh)) {
            const auto time=m_layer->source_->simTimeAt(m_layer->current_time_idx_);
            const bool seek=m_particleSimTime.isValid() && time.isValid()?time<m_particleSimTime:
                m_particleTime>=0 && m_layer->current_time_idx_<m_particleTime && m_layer->source_->historyGeneration()==m_particleGeneration;
            m_worker->request(input,seek);m_particleRevision=m_layer->frame_revision_;m_particleTime=m_layer->current_time_idx_;
            m_particleSimTime=time;m_particleGeneration=m_layer->published_key_.generation;m_particleCutoff=settings.cutoff;
        }
    }
    return m_worker->frame(displayedRevision);
}
} // namespace OpenSWMM::Render
