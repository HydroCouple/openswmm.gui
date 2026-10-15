// GPL-3.0-or-later
#include "render/sublayers/flowstreaksublayer.h"
#include <QSignalBlocker>
#include <algorithm>
#include <cmath>
namespace OpenSWMM::Render {
FlowStreakStyle::FlowStreakStyle(QObject *parent):SublayerStyle(parent) {
    m_scheme.setMode(ClassificationScheme::ClassMode::Continuous);
    m_scheme.setRampName(QStringLiteral("viridis"));
    m_scheme.setRangeMin(0); m_scheme.setRangeMax(2);
}
void FlowStreakStyle::setColor(QColor value) {
    if(!value.isValid()) return;
    if(m_color==value) return; m_color=value; setDirty();
}
void FlowStreakStyle::setWidthPx(double value) {
    if(!std::isfinite(value)) return; value=std::clamp(value,0.1,maximumNumericValue());
    if(m_widthPx==value) return; m_widthPx=value; setDirty();
}
void FlowStreakStyle::setHeadWidthPx(double value) {
    if(!std::isfinite(value)) return; value=std::clamp(value,0.1,maximumNumericValue());
    if(m_headWidthPx==value) return; m_headWidthPx=value; setDirty();
}
double FlowStreakStyle::widthAtFraction(double tailToHead) const {
    // Match the always-tapered particle ribbon and its independent head width.
    const double a=std::clamp(tailToHead,0.,1.);
    const double h=std::clamp((a-.75)*4,0.,1.);
    return m_widthPx*std::max(.25,a)
        +(m_headWidthPx-m_widthPx)*h*h*(3-2*h);
}
void FlowStreakStyle::setTrailDurationSeconds(double value) {
    if(!std::isfinite(value)) return;value=std::clamp(value,.1,maximumNumericValue());
    if(m_trailDurationSeconds==value) return;m_trailDurationSeconds=value;setDirty();
}
void FlowStreakStyle::setSpacingPx(double value) {
    if(!std::isfinite(value)) return; value=std::clamp(value,2.,maximumNumericValue());
    if(m_spacingPx==value) return; m_spacingPx=value; setDirty();
}
void FlowStreakStyle::setAnimated(bool value) {
    if(m_animated==value) return; m_animated=value; setDirty();
}
void FlowStreakStyle::setSpeedMultiplier(double value) {
    if(!std::isfinite(value)) return; value=std::clamp(value,0.,maximumNumericValue());
    m_motionWasMigrated=false;
    if(m_speedMultiplier==value) return; m_speedMultiplier=value; setDirty();
}
void FlowStreakStyle::setMinimumLifetimeSeconds(double value) {
    if(!std::isfinite(value)) return;value=std::clamp(value,.1,maximumNumericValue());
    if(m_minimumLifetimeSeconds==value) return;
    m_minimumLifetimeSeconds=value;m_maximumLifetimeSeconds=std::max(value,m_maximumLifetimeSeconds);setDirty();
}
void FlowStreakStyle::setMaximumLifetimeSeconds(double value) {
    if(!std::isfinite(value)) return;value=std::clamp(value,.1,maximumNumericValue());
    if(m_maximumLifetimeSeconds==value) return;
    m_maximumLifetimeSeconds=value;m_minimumLifetimeSeconds=std::min(value,m_minimumLifetimeSeconds);setDirty();
}
void FlowStreakStyle::setLifetimeFadeSeconds(double value) {
    if(!std::isfinite(value)) return;value=std::clamp(value,0.,maximumNumericValue());
    if(m_lifetimeFadeSeconds==value) return;m_lifetimeFadeSeconds=value;setDirty();
}
void FlowStreakStyle::setColorByMagnitude(bool value) {
    if(m_colorByMagnitude==value) return; m_colorByMagnitude=value; setDirty();
}
void FlowStreakStyle::setScheme(const ClassificationScheme &value) {
    if(m_scheme.toJson()==value.toJson()) return; m_scheme=value;setDirty();
}
QColor FlowStreakStyle::colorForSpeed(double speed) const {
    if(!m_colorByMagnitude) return m_color;
    const auto edges=m_scheme.levelEdges(m_scheme.rangeMin(),m_scheme.rangeMax());
    if(m_scheme.mode()==ClassificationScheme::ClassMode::Classified && edges.size()>1)
        return m_scheme.colorForClass(ClassificationScheme::classIndexFor(speed,edges),edges.size()-1);
    return m_scheme.colorForValue(speed,m_scheme.rangeMin(),m_scheme.rangeMax());
}
QJsonObject FlowStreakStyle::toJson() const {
    return {{"schemaVersion",5},{"classification",m_scheme.toJson()},
        {"color",m_color.name(QColor::HexArgb)},{"widthPx",m_widthPx},{"headWidthPx",m_headWidthPx},
        {"trailDurationSeconds",m_trailDurationSeconds},{"spacingPx",m_spacingPx},{"animated",m_animated},
        {"speedMultiplier",m_speedMultiplier},{"minimumLifetimeSeconds",m_minimumLifetimeSeconds},
        {"maximumLifetimeSeconds",m_maximumLifetimeSeconds},{"lifetimeFadeSeconds",m_lifetimeFadeSeconds},
        {"colorByMagnitude",m_colorByMagnitude}};
}
void FlowStreakStyle::fromJson(const QJsonObject &j) {
    { const QSignalBlocker blocker(this);
      if(j.contains("color")) setColor(QColor(j["color"].toString()));
      if(j.contains("widthPx")) setWidthPx(j["widthPx"].toDouble(m_widthPx));
      if(j.contains("headWidthPx")) setHeadWidthPx(j["headWidthPx"].toDouble(m_headWidthPx));
      else if(j.contains("widthPx")) setHeadWidthPx(m_widthPx);
      if(j.contains("spacingPx")) setSpacingPx(j["spacingPx"].toDouble(m_spacingPx));
      if(j.contains("trailDurationSeconds")) setTrailDurationSeconds(j["trailDurationSeconds"].toDouble(m_trailDurationSeconds));
      if(j.contains("animated")) setAnimated(j["animated"].toBool(m_animated));
      if(j.contains("speedMultiplier")) setSpeedMultiplier(j["speedMultiplier"].toDouble(m_speedMultiplier));
      if(j.contains("minimumLifetimeSeconds")) setMinimumLifetimeSeconds(j["minimumLifetimeSeconds"].toDouble(m_minimumLifetimeSeconds));
      if(j.contains("maximumLifetimeSeconds")) setMaximumLifetimeSeconds(j["maximumLifetimeSeconds"].toDouble(m_maximumLifetimeSeconds));
      if(j.contains("lifetimeFadeSeconds")) setLifetimeFadeSeconds(j["lifetimeFadeSeconds"].toDouble(m_lifetimeFadeSeconds));
      if(j.contains("colorByMagnitude")) setColorByMagnitude(j["colorByMagnitude"].toBool(m_colorByMagnitude));
      if(j.contains("classification")) m_scheme.fromJson(j["classification"].toObject());
      const bool legacy=j["schemaVersion"].toInt()<4 || !j["continuousParticles"].toBool(true);
      m_motionWasMigrated=false;
      if(legacy && !j.isEmpty()) {
          const auto nearest=[](double value,std::initializer_list<double> choices) {
              return *std::min_element(choices.begin(),choices.end(),[=](double a,double b){return std::abs(a-value)<std::abs(b-value);});
          };
          setSpacingPx(nearest(m_spacingPx,{56.,40.,28.}));
          setTrailDurationSeconds(j["velocityScaledTrail"].toBool(j.contains("trailDurationSeconds"))
              ?nearest(m_trailDurationSeconds,{.75,1.5,3.}):1.5);
          setSpeedMultiplier(1);m_motionWasMigrated=true;
      }
    } setDirty();
}
FlowStreakSublayer::FlowStreakSublayer(QObject *parent):ISublayer(parent),m_style(new FlowStreakStyle(this)) {
    connect(m_style,&SublayerStyle::styleChanged,this,&ISublayer::invalidated);
}
void FlowStreakSublayer::setVisible(bool v) { if(m_visible!=v) {m_visible=v;emit invalidated();} }
void FlowStreakSublayer::setOpacity(qreal v) {
    if(!std::isfinite(v)) return;v=std::clamp(v,qreal(0),qreal(1));
    if(m_opacity!=v) {m_opacity=v;emit invalidated();}
}
QList<LegendSymbolItem> FlowStreakSublayer::legendSymbolItems() const {
    QList<LegendSymbolItem> out;
    const int n=m_style->colorByMagnitude()?5:1;
    for(int i=0;i<n;++i) {
        const double v=m_style->scheme().rangeMin()+(m_style->scheme().rangeMax()-m_style->scheme().rangeMin())*i/std::max(1,n-1);
        LegendSymbolItem row;row.sublayerId=id();
        row.label=n==1?tr("Flow Streaks"):tr("%1 m/s").arg(v,0,'g',3);
        SymbolLayer line;line.kind=SymbolLayerKind::SimpleLine;
        SymbolProps::writeColor(line.props,QStringLiteral("color"),m_style->colorForSpeed(v));
        line.props["width"]=m_style->widthPx();row.symbol.layers.append(line);row.symbol.opacity=m_opacity;
        out.append(row);
    } return out;
}
} // namespace OpenSWMM::Render
