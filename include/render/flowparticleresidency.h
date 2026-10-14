// GPL-3.0-or-later
#pragma once
#include "render/flowparticles.h"
#include <QSet>
#include <algorithm>

namespace OpenSWMM::Render {
struct FlowParticleResidency {
    std::vector<quint64> tiles;
    QVector<QRectF> rectangles;
    bool limited=false;
};
// Residency needs only tile geometry, never the source index or its mutable
// query scratch. Copying this description to the worker is constant-sized.
struct FlowParticleResidencyGrid {
    QRectF extent;double cellW=0,cellH=0;int cols=0,rows=0;
    FlowParticleResidencyGrid()=default;
    FlowParticleResidencyGrid(const MeshSpatialGrid &grid)
        :extent(grid.extent),cellW(grid.cellW),cellH(grid.cellH),cols(grid.cols),rows(grid.rows) {}
    bool isEmpty() const {return cols<=0 || rows<=0 || cellW<=0 || cellH<=0;}
};
struct FlowParticleResidencySupport {
    FlowParticleResidency required,prefetch;
    quint64 geometry=0;double clock=0;
};

// Quarter-sized source-index tiles give precise sparse support without a
// domain-sized locator. Heads prefetch two animation seconds; trails pin every
// retained segment, including segments whose intermediate samples were reduced.
template<class Particles>
inline FlowParticleResidency flowParticleResidencyForParticles(QRectF view,const FlowParticleResidencyGrid &grid,
                                                  const Particles &particles,double lookahead=2,bool includeView=true)
{
    FlowParticleResidency out;
    if(grid.isEmpty() || !view.isValid())return out;
    const double dx=grid.cellW/4,dy=grid.cellH/4;
    const int cols=grid.cols*4,rows=grid.rows*4;
    QSet<quint64> keys;
    const auto add=[&](QRectF rect) {
        rect=rect.intersected(grid.extent);
        if(!rect.isValid() || out.limited)return;
        const auto index=[](double value,double origin,double step,int n) {
            return int(std::clamp(std::floor((value-origin)/step),0.,double(n-1)));
        };
        const int x0=index(rect.left(),grid.extent.left(),dx,cols),x1=index(rect.right(),grid.extent.left(),dx,cols);
        const int y0=index(rect.top(),grid.extent.top(),dy,rows),y1=index(rect.bottom(),grid.extent.top(),dy,rows);
        if(quint64(x1-x0+1)*quint64(y1-y0+1)>4096) {out.limited=true;return;}
        for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x) {
            keys.insert(quint64(y)*quint64(cols)+quint64(x));
            if(keys.size()>4096) {out.limited=true;return;}
        }
    };
    const double pad=std::max(view.width(),view.height())*.5;
    if(includeView)add(view.adjusted(-pad,-pad,pad,pad));
    for(const auto &p:particles) {
        if(out.limited)break;
        if(p.stop==FlowParticleStop::None) {
            QPointF velocity;
            if(p.history.size()>1) {
                const auto &a=p.history[p.history.size()-2],&b=p.history.back();
                if(b.age-a.age>1e-9)velocity=(b.position-a.position)/(b.age-a.age);
            }
            const QPointF predicted(std::clamp(p.position.x()+lookahead*velocity.x(),grid.extent.left(),grid.extent.right()),
                                    std::clamp(p.position.y()+lookahead*velocity.y(),grid.extent.top(),grid.extent.bottom()));
            add(QRectF(p.position,predicted).normalized().adjusted(-dx,-dy,dx,dy));
        }
        for(size_t i=1;i<p.history.size();++i) {
            if(p.history[i-1].age<=p.visibleAfterAge)continue;
            const double error=std::max(0.,p.history[i].error);
            add(QRectF(p.history[i-1].position,p.history[i].position).normalized()
                .adjusted(-dx/8-error,-dy/8-error,dx/8+error,dy/8+error));
            if(out.limited)break;
        }
    }
    if(out.limited)return out;
    out.tiles.reserve(keys.size());for(quint64 key:keys)out.tiles.push_back(key);
    std::sort(out.tiles.begin(),out.tiles.end());out.rectangles.reserve(keys.size());
    for(quint64 key:out.tiles)out.rectangles.append({grid.extent.left()+(key%cols)*dx,
        grid.extent.top()+(key/cols)*dy,dx,dy});
    return out;
}
inline FlowParticleResidency flowParticleResidency(QRectF view,const FlowParticleResidencyGrid &grid,
                                                  const FlowParticleFrame *frame,double lookahead=2,bool includeView=true) {
    return frame?flowParticleResidencyForParticles(view,grid,frame->particles,lookahead,includeView)
                :flowParticleResidencyForParticles(view,grid,FlowParticleSlots<FlowParticle>{},lookahead,includeView);
}
inline void mergeFlowParticleResidency(FlowParticleResidency &out,const FlowParticleResidency &support,
                                      const FlowParticleResidencyGrid &grid) {
    if(out.limited || support.limited) {out.limited=true;return;}
    std::vector<quint64> tiles;tiles.reserve(out.tiles.size()+support.tiles.size());
    std::set_union(out.tiles.begin(),out.tiles.end(),support.tiles.begin(),support.tiles.end(),std::back_inserter(tiles));
    if(tiles.size()>4096) {out.limited=true;return;}
    out.tiles=std::move(tiles);out.rectangles.clear();out.rectangles.reserve(out.tiles.size());
    const int cols=grid.cols*4;const double dx=grid.cellW/4,dy=grid.cellH/4;
    for(quint64 key:out.tiles)out.rectangles.append({grid.extent.left()+(key%cols)*dx,
        grid.extent.top()+(key/cols)*dy,dx,dy});
}
} // namespace OpenSWMM::Render
