// GPL-3.0-or-later
#include "layers/swmm2dresultslayer.h"
#include <QPainter>
#include <QLinearGradient>
#include <QPolygonF>
#include <algorithm>
#include <cmath>
#include <limits>

using namespace OpenSWMM::Render;

bool SWMM2DResultsLayer::flowStreaksEnabled() const
{
    const bool field=current_time_idx_>=0 && !m_sceneTris.empty();
    return isVisible() && opacity()>0 && field && m_flowStreakSublayer->isVisible()
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
    flowParticles(view,scale,frame_revision_);m_particleController->waitForDone();
    return bool(flowParticles(view,scale,frame_revision_));
}
QString SWMM2DResultsLayer::flowStreakAvailability() const
{
    if(!source_) return tr("No 2D results available");
    if(source_->isLive() && !liveRenderEnabled()) return tr("Enable Live 2D to update live flow streaks");
    if(!m_flowStreakSublayer->isVisible()) return tr("Show Flow Streaks to start rendering");
    return m_particleController->availability();
}
QString SWMM2DResultsLayer::flowStreakStatus() const
{
    const auto availability=flowStreakAvailability();if(!availability.isEmpty())return availability;
    return (m_flowStreakSublayer->streakStyle()->motionWasMigrated()?tr("Old motion setting reset to Normal. "):QString())
        +m_particleController->status();
}

void SWMM2DResultsLayer::advanceFlowStreaks(double seconds) {m_particleController->advance(seconds);}
std::shared_ptr<const FlowParticleNativeInput> SWMM2DResultsLayer::captureFlowParticleInput(const QRectF &view,double scale) {
    return m_particleController->capture(view,scale);
}
std::shared_ptr<const FlowParticleFrame> SWMM2DResultsLayer::flowParticles(const QRectF &view,double scale,quint64 revision,bool presented) {
    return m_particleController->frame(view,scale,revision,presented);
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
    const auto *style=m_flowStreakSublayer->streakStyle();
    const auto frame=flowParticles(coverage,scale,frame_revision_);if(!frame)return;
    const auto smooth=[](double x){x=std::clamp(x,0.,1.);return x*x*(3-2*x);};
    const double interpolation=flowParticleInterpolation(frame);
    const double clock=frame->previousClock+(frame->clock-frame->previousClock)*interpolation;
    p->save();p->setOpacity(p->opacity()*m_flowStreakSublayer->opacity()*flowParticleOpacity());
    p->setRenderHint(QPainter::Antialiasing,true);p->setPen(Qt::NoPen);
    for(const auto &particle:frame->particles) {
        if(particle.history.empty())continue;
        const double age=particle.previousAge+(particle.age-particle.previousAge)*interpolation;
        const double tail=std::min(style->trailDurationSeconds(),particle.historyLimited
            ?std::max(1e-6,age-particle.history.front().age):style->trailDurationSeconds());
        const double fade=style->lifetimeFadeSeconds();
        std::vector<FlowParticleSample> points;
        for(size_t i=1;i<particle.history.size();++i) {
            const auto &a=particle.history[i-1],&b=particle.history[i];
            const double begin=std::max(a.age,age-tail),end=std::min(b.age,age);
            if(end<=begin || b.age<=a.age)continue;
            const auto at=[&](double t){return FlowParticleSample{
                a.position+(b.position-a.position)*((t-a.age)/(b.age-a.age)),t,b.cell,b.speed};};
            if(points.empty())points.push_back(at(begin));points.push_back(at(end));
        }
        if(points.size()<2)continue;
        QPolygonF left,right;
        const auto unit=[](QPointF v){const double n=std::hypot(v.x(),v.y());return n>1e-12?v/n:QPointF();};
        for(size_t i=0;i<points.size();++i) {
            const auto position=points[i].position;
            const auto before=unit(position-points[i?i-1:i].position);
            const auto after=unit(points[std::min(i+1,points.size()-1)].position-position);
            const auto direction=unit(before+after);QPointF normal(-direction.y(),direction.x());
            const double projection=std::abs(QPointF::dotProduct(normal,QPointF(-after.y(),after.x())));
            normal*=std::min(2.,projection>1e-12?1/projection:1.)
                *style->widthAtFraction(1-(age-points[i].age)/tail)*.5/scale;
            left.append(position-normal);right.prepend(position+normal);
        }
        double opacity=1;const double birthFade=std::min(fade,particle.lifetime*.2);
        if(birthFade>0)opacity*=smooth(age/birthFade)*smooth((particle.lifetime-age)/birthFade);
        if(particle.stoppedAt>=0)opacity*=fade>0?1-smooth((clock-particle.stoppedAt)/fade):0;
        const auto color=style->colorForSpeed(points.back().speed);
        QLinearGradient gradient(points.front().position,points.back().position);
        for(double f:{0.,.25,.5,.75,1.}) {
            auto c=color;c.setAlphaF(c.alphaF()*opacity*f);gradient.setColorAt(f,c);
        }
        left+=right;
        const double luminance=.2126*color.redF()+.7152*color.greenF()+.0722*color.blueF();
        if(luminance>.55) {
            QLinearGradient outline(points.front().position,points.back().position);
            for(double f:{0.,.25,.5,.75,1.}) {
                auto c=QColor::fromRgbF(.08,.20,.28);c.setAlphaF(color.alphaF()*opacity*f*smooth((luminance-.55)/.2)*.65);
                outline.setColorAt(f,c);
            }
            p->setPen(QPen(QBrush(outline),1./scale));p->setBrush(Qt::NoBrush);p->drawPolygon(left);
        }
        p->setPen(Qt::NoPen);p->setBrush(gradient);p->drawPolygon(left);
    }
    p->restore();
}
