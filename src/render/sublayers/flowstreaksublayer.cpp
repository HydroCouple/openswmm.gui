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
    if(!std::isfinite(value)) return; value=std::clamp(value,double(0.1),double(12));
    if(m_widthPx==value) return; m_widthPx=value; setDirty();
}
void FlowStreakStyle::setHeadWidthPx(double value) {
    if(!std::isfinite(value)) return; value=std::clamp(value,double(0.1),double(12));
    if(m_headWidthPx==value) return; m_headWidthPx=value; setDirty();
}
double FlowStreakStyle::widthAtFraction(double tailToHead) const {
    // Match flowstreak.vert: blend the independent head width over the leading
    // quarter, retaining the original taper everywhere when both widths match.
    const double a=std::clamp(tailToHead,0.,1.);
    const double h=std::clamp((a-.75)*4,0.,1.);
    return m_widthPx*(m_taper?std::max(.15,a):1.)
        +(m_headWidthPx-m_widthPx)*h*h*(3-2*h);
}
void FlowStreakStyle::setTrailLengthPx(double value) {
    if(!std::isfinite(value)) return; value=std::clamp(value,double(1),double(200));
    if(m_trailLengthPx==value) return; m_trailLengthPx=value; setDirty();
}
void FlowStreakStyle::setFade(bool value) {
    if(m_fade==value) return; m_fade=value; setDirty();
}
void FlowStreakStyle::setTaper(bool value) {
    if(m_taper==value) return; m_taper=value; setDirty();
}
void FlowStreakStyle::setSpacingPx(double value) {
    if(!std::isfinite(value)) return; value=std::clamp(value,double(8),double(200));
    if(m_spacingPx==value) return; m_spacingPx=value; setDirty();
}
void FlowStreakStyle::setPathLengthPx(double value) {
    if(!std::isfinite(value)) return; value=std::clamp(value,double(16),double(512));
    if(m_pathLengthPx==value) return; m_pathLengthPx=value; setDirty();
}
void FlowStreakStyle::setDryDepthCutoff(double value) {
    if(!std::isfinite(value)) return; value=std::clamp(value,double(0),double(10));
    if(m_dryDepthCutoff==value) return; m_dryDepthCutoff=value; setDirty();
}
void FlowStreakStyle::setAnimated(bool value) {
    if(m_animated==value) return; m_animated=value; setDirty();
}
void FlowStreakStyle::setSpeedMultiplier(double value) {
    if(!std::isfinite(value)) return; value=std::clamp(value,double(0),double(1000));
    if(m_speedMultiplier==value) return; m_speedMultiplier=value; setDirty();
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
    QJsonObject j{{"schemaVersion",1},{"classification",m_scheme.toJson()}};
    j["color"]=m_color.name(QColor::HexArgb);
    j["widthPx"]=m_widthPx;
    j["headWidthPx"]=m_headWidthPx;
    j["trailLengthPx"]=m_trailLengthPx;
    j["fade"]=m_fade;
    j["taper"]=m_taper;
    j["spacingPx"]=m_spacingPx;
    j["pathLengthPx"]=m_pathLengthPx;
    j["dryDepthCutoff"]=m_dryDepthCutoff;
    j["animated"]=m_animated;
    j["speedMultiplier"]=m_speedMultiplier;
    j["colorByMagnitude"]=m_colorByMagnitude;
    return j;
}
void FlowStreakStyle::fromJson(const QJsonObject &j) {
    { const QSignalBlocker blocker(this);
      if(j.contains("color")) setColor(QColor(j["color"].toString()));
      if(j.contains("widthPx")) setWidthPx(j["widthPx"].toDouble(m_widthPx));
      if(j.contains("headWidthPx")) setHeadWidthPx(j["headWidthPx"].toDouble(m_headWidthPx));
      else if(j.contains("widthPx")) setHeadWidthPx(m_widthPx); // legacy styles used one width
      if(j.contains("trailLengthPx")) setTrailLengthPx(j["trailLengthPx"].toDouble(m_trailLengthPx));
      if(j.contains("fade")) setFade(j["fade"].toBool(m_fade));
      if(j.contains("taper")) setTaper(j["taper"].toBool(m_taper));
      if(j.contains("spacingPx")) setSpacingPx(j["spacingPx"].toDouble(m_spacingPx));
      if(j.contains("pathLengthPx")) setPathLengthPx(j["pathLengthPx"].toDouble(m_pathLengthPx));
      if(j.contains("dryDepthCutoff")) setDryDepthCutoff(j["dryDepthCutoff"].toDouble(m_dryDepthCutoff));
      if(j.contains("animated")) setAnimated(j["animated"].toBool(m_animated));
      if(j.contains("speedMultiplier")) setSpeedMultiplier(j["speedMultiplier"].toDouble(m_speedMultiplier));
      if(j.contains("colorByMagnitude")) setColorByMagnitude(j["colorByMagnitude"].toBool(m_colorByMagnitude));
      if(j.contains("classification")) m_scheme.fromJson(j["classification"].toObject());
    } setDirty();
}
QJsonObject FlowStreakStyle::pathSettings() const {
    return {{"spacing",m_spacingPx},{"length",m_pathLengthPx},{"cutoff",m_dryDepthCutoff}};
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
