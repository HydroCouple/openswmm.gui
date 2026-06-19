/*!
 * \file   velocityvectorsublayer.cpp
 * \brief  Slice S5.3.
 */
#include "render/sublayers/velocityvectorsublayer.h"

#include <QJsonObject>

#include <algorithm>
#include <cmath>

namespace OpenSWMM::Render
{

namespace { void clampNonNeg(double &v) { if (v < 0.0) v = 0.0; } }

void VelocityVectorStyle::setGlyphLengthScalePxPerMps(double v) { clampNonNeg(v); if (!qFuzzyCompare(m_glyphLengthScalePxPerMps+1.0, v+1.0)) { m_glyphLengthScalePxPerMps = v; setDirty(); } }
void VelocityVectorStyle::setLengthScaling(LengthScaling v)     { if (m_lengthScaling != v) { m_lengthScaling = v; setDirty(); } }
void VelocityVectorStyle::setShaftWidthPx(double v)             { if (v < 0.1) v = 0.1; if (!qFuzzyCompare(m_shaftWidthPx+1.0, v+1.0)) { m_shaftWidthPx = v; setDirty(); } }
void VelocityVectorStyle::setColorClassCount(int v)             { if (v < 0) v = 0; if (v == 1) v = 2; if (m_colorClassCount != v) { m_colorClassCount = v; setDirty(); } }
void VelocityVectorStyle::setGlyphLengthMinPx(double v)         { clampNonNeg(v); if (!qFuzzyCompare(m_glyphLengthMinPx+1.0,         v+1.0)) { m_glyphLengthMinPx         = v; setDirty(); } }
void VelocityVectorStyle::setGlyphLengthMaxPx(double v)         { clampNonNeg(v); if (!qFuzzyCompare(m_glyphLengthMaxPx+1.0,         v+1.0)) { m_glyphLengthMaxPx         = v; setDirty(); } }
void VelocityVectorStyle::setGlyphSpacingPx(double v)           { if (v < 1.0) v = 1.0; if (!qFuzzyCompare(m_glyphSpacingPx+1.0,    v+1.0)) { m_glyphSpacingPx           = v; setDirty(); } }
void VelocityVectorStyle::setHeadSizePx(double v)               { clampNonNeg(v); if (!qFuzzyCompare(m_headSizePx+1.0,               v+1.0)) { m_headSizePx               = v; setDirty(); } }
void VelocityVectorStyle::setDryDepthCutoff(double v)           { clampNonNeg(v); if (!qFuzzyCompare(m_dryDepthCutoff+1.0,           v+1.0)) { m_dryDepthCutoff           = v; setDirty(); } }

void VelocityVectorStyle::setColor(const QColor &v)
{
    if (m_color == v) return;
    m_color = v;
    setDirty();
}

void VelocityVectorStyle::setColorByMagnitude(bool v)
{
    if (m_colorByMagnitude == v) return;
    m_colorByMagnitude = v;
    setDirty();
}

void VelocityVectorStyle::setColorRampName(const QString &v)
{
    if (m_colorRampName == v) return;
    m_colorRampName = v;
    setDirty();
}

void VelocityVectorStyle::setSpeedMinMps(double v)
{
    if (qFuzzyCompare(m_speedMinMps + 1.0, v + 1.0)) return;
    m_speedMinMps = v;
    setDirty();
}

void VelocityVectorStyle::setSpeedMaxMps(double v)
{
    if (qFuzzyCompare(m_speedMaxMps + 1.0, v + 1.0)) return;
    m_speedMaxMps = v;
    setDirty();
}

QColor VelocityVectorStyle::colorForSpeed(double speedMps) const
{
    if (!m_colorByMagnitude)
        return m_color;
    RasterColorRamp ramp = RasterColorRamp::builtin(m_colorRampName);
    ramp.minValue = m_speedMinMps;
    ramp.maxValue = (m_speedMaxMps > m_speedMinMps) ? m_speedMaxMps
                                                    : m_speedMinMps + 1.0;
    if (m_colorClassCount >= 2) {
        // VS.8 — discrete colour bands: quantise the normalised position to
        // the containing band's midpoint so every speed in a band shares one
        // colour (matches the legend rows exactly).
        double f = (speedMps - ramp.minValue) / (ramp.maxValue - ramp.minValue);
        f = std::clamp(f, 0.0, 1.0);
        const int n   = m_colorClassCount;
        const int bin = std::min(n - 1, int(f * double(n)));
        return ramp.colorAt((double(bin) + 0.5) / double(n));
    }
    return ramp.colorForValue(speedMps);
}

double VelocityVectorStyle::glyphLengthPxForSpeed(double speedMps) const
{
    if (speedMps <= 0.0) return 0.0;
    double px = 0.0;
    switch (m_lengthScaling) {
    case LengthScaling::Linear:
        px = m_glyphLengthScalePxPerMps * speedMps;
        break;
    case LengthScaling::SquareRoot:
        px = m_glyphLengthScalePxPerMps * std::sqrt(speedMps);
        break;
    case LengthScaling::Log:
        px = m_glyphLengthScalePxPerMps * std::log1p(speedMps);
        break;
    }
    const double lo = std::min(m_glyphLengthMinPx, m_glyphLengthMaxPx);
    const double hi = std::max(m_glyphLengthMinPx, m_glyphLengthMaxPx);
    return std::clamp(px, lo, hi > 0.0 ? hi : px);
}

QJsonObject VelocityVectorStyle::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("glyphLengthScalePxPerMps"), m_glyphLengthScalePxPerMps);
    obj.insert(QStringLiteral("lengthScaling"),            int(m_lengthScaling));
    obj.insert(QStringLiteral("glyphLengthMinPx"),         m_glyphLengthMinPx);
    obj.insert(QStringLiteral("glyphLengthMaxPx"),         m_glyphLengthMaxPx);
    obj.insert(QStringLiteral("glyphSpacingPx"),           m_glyphSpacingPx);
    obj.insert(QStringLiteral("headSizePx"),               m_headSizePx);
    obj.insert(QStringLiteral("shaftWidthPx"),             m_shaftWidthPx);
    obj.insert(QStringLiteral("color"),                    m_color.name(QColor::HexArgb));
    obj.insert(QStringLiteral("dryDepthCutoff"),           m_dryDepthCutoff);
    obj.insert(QStringLiteral("colorByMagnitude"),         m_colorByMagnitude);
    obj.insert(QStringLiteral("colorRampName"),            m_colorRampName);
    obj.insert(QStringLiteral("speedMinMps"),              m_speedMinMps);
    obj.insert(QStringLiteral("speedMaxMps"),              m_speedMaxMps);
    obj.insert(QStringLiteral("colorClassCount"),          m_colorClassCount);
    return obj;
}

void VelocityVectorStyle::fromJson(const QJsonObject &j)
{
    m_glyphLengthScalePxPerMps = j.value(QStringLiteral("glyphLengthScalePxPerMps")).toDouble(m_glyphLengthScalePxPerMps);
    m_glyphLengthMinPx         = j.value(QStringLiteral("glyphLengthMinPx")).toDouble(m_glyphLengthMinPx);
    m_glyphLengthMaxPx         = j.value(QStringLiteral("glyphLengthMaxPx")).toDouble(m_glyphLengthMaxPx);
    m_glyphSpacingPx           = j.value(QStringLiteral("glyphSpacingPx")).toDouble(m_glyphSpacingPx);
    m_headSizePx               = j.value(QStringLiteral("headSizePx")).toDouble(m_headSizePx);
    m_dryDepthCutoff           = j.value(QStringLiteral("dryDepthCutoff")).toDouble(m_dryDepthCutoff);

    if (m_glyphSpacingPx < 1.0) m_glyphSpacingPx = 1.0;
    clampNonNeg(m_glyphLengthScalePxPerMps);
    clampNonNeg(m_glyphLengthMinPx);
    clampNonNeg(m_glyphLengthMaxPx);
    clampNonNeg(m_headSizePx);
    clampNonNeg(m_dryDepthCutoff);

    const QString tok = j.value(QStringLiteral("color")).toString();
    if (!tok.isEmpty()) { const QColor c(tok); if (c.isValid()) m_color = c; }

    m_colorByMagnitude = j.value(QStringLiteral("colorByMagnitude")).toBool(m_colorByMagnitude);
    m_colorRampName    = j.value(QStringLiteral("colorRampName")).toString(m_colorRampName);
    m_speedMinMps      = j.value(QStringLiteral("speedMinMps")).toDouble(m_speedMinMps);
    m_speedMaxMps      = j.value(QStringLiteral("speedMaxMps")).toDouble(m_speedMaxMps);

    const int scaling = j.value(QStringLiteral("lengthScaling")).toInt(int(m_lengthScaling));
    if (scaling >= int(LengthScaling::Linear) && scaling <= int(LengthScaling::Log))
        m_lengthScaling = LengthScaling(scaling);
    m_shaftWidthPx = j.value(QStringLiteral("shaftWidthPx")).toDouble(m_shaftWidthPx);
    if (m_shaftWidthPx < 0.1) m_shaftWidthPx = 0.1;
    m_colorClassCount = j.value(QStringLiteral("colorClassCount")).toInt(m_colorClassCount);
    if (m_colorClassCount < 0) m_colorClassCount = 0;
    if (m_colorClassCount == 1) m_colorClassCount = 2;
    setDirty();
}

VelocityVectorSublayer::VelocityVectorSublayer(QString id_, QObject *parent)
    : ISublayer(parent), m_id(std::move(id_)), m_style(new VelocityVectorStyle(this))
{
    connect(m_style, &SublayerStyle::styleChanged, this, &ISublayer::invalidated);
}

void VelocityVectorSublayer::setVisible(bool v)
{
    if (m_visible == v) return;
    m_visible = v;
    emit invalidated();
}

void VelocityVectorSublayer::setOpacity(qreal o)
{
    if (o < 0.0) o = 0.0;
    if (o > 1.0) o = 1.0;
    if (qFuzzyCompare(m_opacity + 1.0, o + 1.0)) return;
    m_opacity = o;
    emit invalidated();
}

QList<LegendSymbolItem> VelocityVectorSublayer::legendSymbolItems() const
{
    auto makeArrowRow = [this](const QString &label, const QColor &color) {
        LegendSymbolItem item;
        item.label      = label;
        item.sublayerId = m_id;
        SymbolLayer arrow;
        arrow.kind = SymbolLayerKind::SimpleMarker;
        arrow.props.insert(QStringLiteral("shape"), QStringLiteral("arrow"));
        SymbolProps::writeColor(arrow.props, QStringLiteral("color"), color);
        arrow.props.insert(QStringLiteral("size"),  m_style->headSizePx());
        item.symbol.layers.append(arrow);
        item.symbol.opacity = m_opacity;
        return item;
    };

    if (!m_style->colorByMagnitude())
        return { makeArrowRow(tr("Velocity"), m_style->color()) };

    // VS.8 — magnitude-coloured arrows get one legend row per colour band
    // (discrete classes) or a fixed 5-row gradient preview (continuous),
    // mirroring how the graduated renderers present their ramps.
    const double lo = m_style->speedMinMps();
    const double hi = std::max(m_style->speedMaxMps(), lo + 1e-9);
    const int    n  = (m_style->colorClassCount() >= 2)
                        ? m_style->colorClassCount() : 5;

    QList<LegendSymbolItem> out;
    {
        LegendSymbolItem header;
        header.label      = tr("Velocity (m/s)");
        header.sublayerId = m_id;
        out.append(header);
    }
    for (int i = 0; i < n; ++i) {
        const double bandLo = lo + (hi - lo) *  double(i)      / double(n);
        const double bandHi = lo + (hi - lo) * (double(i) + 1) / double(n);
        const double mid    = 0.5 * (bandLo + bandHi);
        LegendSymbolItem item = makeArrowRow(
            QStringLiteral("%1 – %2").arg(bandLo, 0, 'g', 3).arg(bandHi, 0, 'g', 3),
            m_style->colorForSpeed(mid));
        item.range    = { bandLo, bandHi };
        item.classKey = QString::number(i);
        out.append(item);
    }
    return out;
}

QSGNode *VelocityVectorSublayer::buildOrUpdateNode(QSGNode *existing, const SublayerContext &ctx)
{
    Q_UNUSED(ctx);
    return existing;
}

} // namespace OpenSWMM::Render
