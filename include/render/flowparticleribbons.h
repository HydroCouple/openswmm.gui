// GPL-3.0-or-later
#pragma once
#include <QPointF>
#include <QRectF>
#include <QColor>
#include <array>
#include <functional>
#include <vector>
#include <algorithm>

namespace OpenSWMM::Render {
struct FlowParticleFrame;
struct FlowParticleVertex {
    float x,y,nx,ny,age,particle;
    unsigned char r,g,b,a;
};
static_assert(sizeof(FlowParticleVertex)==28);
// Exact float bits in RGBA8 texels avoid float-texture/backend requirements.
// Previous/current age, life, stop clock, tail age, and previous/current visibility.
using FlowParticleTiming=std::array<std::array<unsigned char,4>,8>;
// All fields are copied on the GUI thread. A palette callback must own its
// data; it must never borrow a mutable QObject from the worker thread.
struct FlowParticleAppearance {
    QPointF anchor;
    QRectF viewport;
    QColor color=Qt::white;
    double opacity=1;
    bool fade=true,taper=true;
    quint64 revision=0;
    int particleCapacity=0,historyCapacity=0;
    std::function<QColor(double)> colorForSpeed;
};
struct FlowParticleRibbon {
    std::vector<FlowParticleVertex> vertices;
    std::vector<FlowParticleTiming> timing;
    std::vector<quint64> particleIds;
    QPointF anchor;
    QRectF viewport;
    QColor color;
    double scale=0,buildMs=0;
    quint64 revision=0,trackedId=0;
    int particles=0,history=0;
};
float flowParticleTimingValue(const FlowParticleTiming &,size_t column);
inline double flowParticleTrailVisibility(double pixels) {
    const double t=std::clamp((pixels-2.)/2.,0.,1.);
    return t*t*(3-2*t);
}
void buildFlowParticleRibbon(const FlowParticleFrame &,double scale,quint64 trackedId,
                            const FlowParticleAppearance &,FlowParticleRibbon &,const FlowParticleFrame *previous=nullptr);
} // namespace OpenSWMM::Render
