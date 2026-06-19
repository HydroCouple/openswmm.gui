/*!
 * \file   depthcolorrampsublayer.cpp
 * \brief  Slice S5.2.
 */
#include "render/sublayers/depthcolorrampsublayer.h"

#include "render/colorramp.h"

#include <QJsonObject>

#include <algorithm>
#include <cmath>

namespace OpenSWMM::Render
{

DepthColorRampStyle::DepthColorRampStyle(QObject *parent) : SublayerStyle(parent)
{
    // Defaults reproduce the pre-US.2 depth fill: smooth (Continuous) two-
    // colour blue gradient over [0,1].
    m_scheme.setMode(ClassificationScheme::ClassMode::Continuous);
    m_scheme.setRampName(QString());                       // "" = two-colour
    m_scheme.setLowColor(QColor( 60, 100, 200, 200));      // bluish
    m_scheme.setHighColor(QColor(200, 220, 255, 200));     // pale blue
    m_scheme.setRangeMin(0.0);
    m_scheme.setRangeMax(1.0);
    m_scheme.setClassCount(5);
}

void DepthColorRampStyle::setAttribute(const QString &v)
{
    if (m_attribute == v) return;
    m_attribute = v;
    setDirty();
}

void DepthColorRampStyle::setMinValue(double v)
{
    if (qFuzzyCompare(m_scheme.rangeMin() + 1.0, v + 1.0)) return;
    m_scheme.setRangeMin(v);
    setDirty();
}

void DepthColorRampStyle::setMaxValue(double v)
{
    if (qFuzzyCompare(m_scheme.rangeMax() + 1.0, v + 1.0)) return;
    m_scheme.setRangeMax(v);
    setDirty();
}

void DepthColorRampStyle::setColorRampName(const QString &v) { if (m_scheme.rampName()  != v) { m_scheme.setRampName(v);  setDirty(); } }
void DepthColorRampStyle::setInvertRamp(bool v)              { if (m_scheme.invertRamp() != v) { m_scheme.setInvertRamp(v); setDirty(); } }
void DepthColorRampStyle::setLowColor(const QColor &v)      { if (m_scheme.lowColor()  != v) { m_scheme.setLowColor(v);  setDirty(); } }
void DepthColorRampStyle::setHighColor(const QColor &v)     { if (m_scheme.highColor() != v) { m_scheme.setHighColor(v); setDirty(); } }
void DepthColorRampStyle::setBelowMinColor(const QColor &v) { if (m_belowMinColor != v) { m_belowMinColor = v; setDirty(); } }
void DepthColorRampStyle::setAboveMaxColor(const QColor &v) { if (m_aboveMaxColor != v) { m_aboveMaxColor = v; setDirty(); } }

void DepthColorRampStyle::setUseLogScale(bool v)
{
    if (m_useLogScale == v) return;
    m_useLogScale = v;
    setDirty();
}

void DepthColorRampStyle::setScheme(const ClassificationScheme &s)
{
    if (m_scheme == s) return;
    m_scheme = s;
    setDirty();
}

QColor DepthColorRampStyle::colorAtF(double f) const
{
    return m_scheme.colorAtF(f);
}

QJsonObject DepthColorRampStyle::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("attribute"),     m_attribute);
    obj.insert(QStringLiteral("belowMinColor"), m_belowMinColor.name(QColor::HexArgb));
    obj.insert(QStringLiteral("aboveMaxColor"), m_aboveMaxColor.name(QColor::HexArgb));
    obj.insert(QStringLiteral("useLogScale"),   m_useLogScale);
    obj.insert(QStringLiteral("classification"), m_scheme.toJson());
    return obj;
}

void DepthColorRampStyle::fromJson(const QJsonObject &j)
{
    m_attribute   = j.value(QStringLiteral("attribute")).toString(m_attribute);
    m_useLogScale = j.value(QStringLiteral("useLogScale")).toBool(m_useLogScale);

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

DepthColorRampSublayer::DepthColorRampSublayer(QString id_, QObject *parent)
    : ISublayer(parent), m_id(std::move(id_)), m_style(new DepthColorRampStyle(this))
{
    connect(m_style, &SublayerStyle::styleChanged, this, &ISublayer::invalidated);
}

void DepthColorRampSublayer::setVisible(bool v)
{
    if (m_visible == v) return;
    m_visible = v;
    emit invalidated();
}

void DepthColorRampSublayer::setOpacity(qreal o)
{
    if (o < 0.0) o = 0.0;
    if (o > 1.0) o = 1.0;
    if (qFuzzyCompare(m_opacity + 1.0, o + 1.0)) return;
    m_opacity = o;
    emit invalidated();
}

QList<LegendSymbolItem> DepthColorRampSublayer::legendSymbolItems() const
{
    // Emit two bookend rows so the legend has a gradient bar's low and
    // high anchor points; the LegendRenderer interpolates between them.
    QList<LegendSymbolItem> out;

    LegendSymbolItem lo;
    lo.label      = QStringLiteral("%1 = %2").arg(m_style->attribute()).arg(m_style->minValue());
    lo.sublayerId = m_id;
    lo.range      = { m_style->minValue(), m_style->minValue() };
    SymbolLayer loFill;
    loFill.kind = SymbolLayerKind::SimpleFill;
    SymbolProps::writeColor(loFill.props, QStringLiteral("color"), m_style->lowColor());
    lo.symbol.layers.append(loFill);
    lo.symbol.opacity = m_opacity;
    out.append(lo);

    LegendSymbolItem hi;
    hi.label      = QStringLiteral("%1 = %2").arg(m_style->attribute()).arg(m_style->maxValue());
    hi.sublayerId = m_id;
    hi.range      = { m_style->maxValue(), m_style->maxValue() };
    SymbolLayer hiFill;
    hiFill.kind = SymbolLayerKind::SimpleFill;
    SymbolProps::writeColor(hiFill.props, QStringLiteral("color"), m_style->highColor());
    hi.symbol.layers.append(hiFill);
    hi.symbol.opacity = m_opacity;
    out.append(hi);

    return out;
}

QSGNode *DepthColorRampSublayer::buildOrUpdateNode(QSGNode *existing, const SublayerContext &ctx)
{
    Q_UNUSED(ctx);
    return existing;
}

} // namespace OpenSWMM::Render
