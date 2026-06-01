/*!
 * \file   depthcolorrampsublayer.cpp
 * \brief  Slice S5.2.
 */
#include "render/sublayers/depthcolorrampsublayer.h"

#include <QJsonObject>

namespace OpenSWMM::Render
{

void DepthColorRampStyle::setAttribute(const QString &v)
{
    if (m_attribute == v) return;
    m_attribute = v;
    setDirty();
}

void DepthColorRampStyle::setMinValue(double v)
{
    if (qFuzzyCompare(m_minValue + 1.0, v + 1.0)) return;
    m_minValue = v;
    setDirty();
}

void DepthColorRampStyle::setMaxValue(double v)
{
    if (qFuzzyCompare(m_maxValue + 1.0, v + 1.0)) return;
    m_maxValue = v;
    setDirty();
}

void DepthColorRampStyle::setLowColor(const QColor &v)      { if (m_lowColor      != v) { m_lowColor      = v; setDirty(); } }
void DepthColorRampStyle::setHighColor(const QColor &v)     { if (m_highColor     != v) { m_highColor     = v; setDirty(); } }
void DepthColorRampStyle::setBelowMinColor(const QColor &v) { if (m_belowMinColor != v) { m_belowMinColor = v; setDirty(); } }
void DepthColorRampStyle::setAboveMaxColor(const QColor &v) { if (m_aboveMaxColor != v) { m_aboveMaxColor = v; setDirty(); } }

void DepthColorRampStyle::setUseLogScale(bool v)
{
    if (m_useLogScale == v) return;
    m_useLogScale = v;
    setDirty();
}

QJsonObject DepthColorRampStyle::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("attribute"),     m_attribute);
    obj.insert(QStringLiteral("minValue"),      m_minValue);
    obj.insert(QStringLiteral("maxValue"),      m_maxValue);
    obj.insert(QStringLiteral("lowColor"),      m_lowColor.name(QColor::HexArgb));
    obj.insert(QStringLiteral("highColor"),     m_highColor.name(QColor::HexArgb));
    obj.insert(QStringLiteral("belowMinColor"), m_belowMinColor.name(QColor::HexArgb));
    obj.insert(QStringLiteral("aboveMaxColor"), m_aboveMaxColor.name(QColor::HexArgb));
    obj.insert(QStringLiteral("useLogScale"),   m_useLogScale);
    return obj;
}

void DepthColorRampStyle::fromJson(const QJsonObject &j)
{
    m_attribute   = j.value(QStringLiteral("attribute")).toString(m_attribute);
    m_minValue    = j.value(QStringLiteral("minValue")).toDouble(m_minValue);
    m_maxValue    = j.value(QStringLiteral("maxValue")).toDouble(m_maxValue);
    m_useLogScale = j.value(QStringLiteral("useLogScale")).toBool(m_useLogScale);

    auto readColor = [&](const char *key, QColor &slot) {
        const QString t = j.value(QString::fromLatin1(key)).toString();
        if (!t.isEmpty()) { const QColor c(t); if (c.isValid()) slot = c; }
    };
    readColor("lowColor",      m_lowColor);
    readColor("highColor",     m_highColor);
    readColor("belowMinColor", m_belowMinColor);
    readColor("aboveMaxColor", m_aboveMaxColor);

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
    loFill.props.insert(QStringLiteral("color"), m_style->lowColor().name(QColor::HexArgb));
    lo.symbol.layers.append(loFill);
    lo.symbol.opacity = m_opacity;
    out.append(lo);

    LegendSymbolItem hi;
    hi.label      = QStringLiteral("%1 = %2").arg(m_style->attribute()).arg(m_style->maxValue());
    hi.sublayerId = m_id;
    hi.range      = { m_style->maxValue(), m_style->maxValue() };
    SymbolLayer hiFill;
    hiFill.kind = SymbolLayerKind::SimpleFill;
    hiFill.props.insert(QStringLiteral("color"), m_style->highColor().name(QColor::HexArgb));
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
