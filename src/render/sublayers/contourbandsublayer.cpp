/*!
 * \file   contourbandsublayer.cpp
 * \brief  Slice S5.5.
 */
#include "render/sublayers/contourbandsublayer.h"

#include "render/colorramp.h"

#include <QJsonObject>

#include <algorithm>
#include <cmath>

namespace OpenSWMM::Render
{

ContourBandStyle::ContourBandStyle(QObject *parent) : SublayerStyle(parent)
{
    // Defaults reproduce the pre-US.2 band output: 8 equal-interval bands
    // over the viridis ramp.
    m_scheme.setMode(ClassificationScheme::ClassMode::Classified);
    m_scheme.setMethod(BinMethod::EqualInterval);
    m_scheme.setClassCount(8);
    m_scheme.setRampName(QStringLiteral("viridis"));
    m_scheme.setLowColor(QColor( 60, 100, 200, 200));
    m_scheme.setHighColor(QColor(200, 220, 255, 200));
}

void ContourBandStyle::setAttribute(const QString &v)        { if (m_attribute     != v) { m_attribute     = v; setDirty(); } }
void ContourBandStyle::setBandCount(int v)
{
    if (v < 1) v = 1;
    if (m_scheme.classCount() == v) return;
    m_scheme.setClassCount(v);
    setDirty();
}
void ContourBandStyle::setColorRampName(const QString &v)   { if (m_scheme.rampName()  != v) { m_scheme.setRampName(v);  setDirty(); } }
void ContourBandStyle::setInvertRamp(bool v)                { if (m_scheme.invertRamp() != v) { m_scheme.setInvertRamp(v); setDirty(); } }
void ContourBandStyle::setLowColor(const QColor &v)         { if (m_scheme.lowColor()  != v) { m_scheme.setLowColor(v);  setDirty(); } }
void ContourBandStyle::setHighColor(const QColor &v)        { if (m_scheme.highColor() != v) { m_scheme.setHighColor(v); setDirty(); } }
void ContourBandStyle::setBelowMinColor(const QColor &v)    { if (m_belowMinColor != v) { m_belowMinColor = v; setDirty(); } }
void ContourBandStyle::setAboveMaxColor(const QColor &v)    { if (m_aboveMaxColor != v) { m_aboveMaxColor = v; setDirty(); } }
void ContourBandStyle::setSmoothBands(bool v)               { if (m_smoothBands   != v) { m_smoothBands   = v; setDirty(); } }
void ContourBandStyle::setUseCustomRange(bool v)            { if (m_scheme.useCustomRange() != v) { m_scheme.setUseCustomRange(v); setDirty(); } }
void ContourBandStyle::setRangeMin(double v)                { if (!qFuzzyCompare(m_scheme.rangeMin() + 1.0, v + 1.0)) { m_scheme.setRangeMin(v); setDirty(); } }
void ContourBandStyle::setRangeMax(double v)                { if (!qFuzzyCompare(m_scheme.rangeMax() + 1.0, v + 1.0)) { m_scheme.setRangeMax(v); setDirty(); } }

void ContourBandStyle::setScheme(const ClassificationScheme &s)
{
    if (m_scheme == s) return;
    m_scheme = s;
    setDirty();
}

QColor ContourBandStyle::colorForBand(int bandIndex, int bandCount) const
{
    return m_scheme.colorForClass(bandIndex, std::max(1, bandCount));
}

QJsonObject ContourBandStyle::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("attribute"),     m_attribute);
    obj.insert(QStringLiteral("belowMinColor"), m_belowMinColor.name(QColor::HexArgb));
    obj.insert(QStringLiteral("aboveMaxColor"), m_aboveMaxColor.name(QColor::HexArgb));
    obj.insert(QStringLiteral("smoothBands"),   m_smoothBands);
    obj.insert(QStringLiteral("classification"), m_scheme.toJson());
    return obj;
}

void ContourBandStyle::fromJson(const QJsonObject &j)
{
    m_attribute   = j.value(QStringLiteral("attribute")).toString(m_attribute);
    m_smoothBands = j.value(QStringLiteral("smoothBands")).toBool(m_smoothBands);

    auto readColor = [&](const char *key, QColor &slot) {
        const QString t = j.value(QString::fromLatin1(key)).toString();
        if (!t.isEmpty()) { const QColor c(t); if (c.isValid()) slot = c; }
    };
    readColor("belowMinColor", m_belowMinColor);
    readColor("aboveMaxColor", m_aboveMaxColor);

    if (j.contains(QStringLiteral("classification")))
        m_scheme = ClassificationScheme::fromJson(j.value(QStringLiteral("classification")).toObject());

    setDirty();
}

ContourBandSublayer::ContourBandSublayer(QString id_, QObject *parent)
    : ISublayer(parent), m_id(std::move(id_)), m_style(new ContourBandStyle(this))
{
    connect(m_style, &SublayerStyle::styleChanged, this, &ISublayer::invalidated);
}

void ContourBandSublayer::setVisible(bool v)
{
    if (m_visible == v) return;
    m_visible = v;
    emit invalidated();
}

void ContourBandSublayer::setOpacity(qreal o)
{
    if (o < 0.0) o = 0.0;
    if (o > 1.0) o = 1.0;
    if (qFuzzyCompare(m_opacity + 1.0, o + 1.0)) return;
    m_opacity = o;
    emit invalidated();
}

QList<LegendSymbolItem> ContourBandSublayer::legendSymbolItems() const
{
    // VS.8 — one swatch row per band, sampled from the active colour
    // source (ramp or two-colour gradient). Value labels are only known
    // when a custom range is set; the layer-level legend builder
    // (SWMM2DResultsLayer::sublayerLegendItems) supplies value-labelled
    // rows for the auto-range case.
    QList<LegendSymbolItem> out;
    const int n = std::max(1, m_style->bandCount());
    const bool haveRange = m_style->useCustomRange()
                           && m_style->rangeMax() > m_style->rangeMin();

    for (int i = 0; i < n; ++i) {
        LegendSymbolItem item;
        if (haveRange) {
            const double lo = m_style->rangeMin()
                + (m_style->rangeMax() - m_style->rangeMin()) * double(i) / n;
            const double hi = m_style->rangeMin()
                + (m_style->rangeMax() - m_style->rangeMin()) * double(i + 1) / n;
            item.label = QStringLiteral("%1 – %2").arg(lo, 0, 'g', 3).arg(hi, 0, 'g', 3);
            item.range = { lo, hi };
        } else {
            item.label = tr("Band %1").arg(i + 1);
        }
        item.sublayerId = m_id;
        item.classKey   = QString::number(i);
        SymbolLayer fill;
        fill.kind = SymbolLayerKind::SimpleFill;
        SymbolProps::writeColor(fill.props, QStringLiteral("color"),
                                m_style->colorForBand(i, n));
        item.symbol.layers.append(fill);
        item.symbol.opacity = m_opacity;
        out.append(item);
    }
    return out;
}

QSGNode *ContourBandSublayer::buildOrUpdateNode(QSGNode *existing, const SublayerContext &ctx)
{
    Q_UNUSED(ctx);
    return existing;
}

} // namespace OpenSWMM::Render
