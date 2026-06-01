/*!
 * \file   contourbandsublayer.cpp
 * \brief  Slice S5.5.
 */
#include "render/sublayers/contourbandsublayer.h"

#include <QJsonObject>

namespace OpenSWMM::Render
{

void ContourBandStyle::setAttribute(const QString &v)        { if (m_attribute     != v) { m_attribute     = v; setDirty(); } }
void ContourBandStyle::setBandCount(int v)
{
    if (v < 1) v = 1;
    if (m_bandCount == v) return;
    m_bandCount = v;
    setDirty();
}
void ContourBandStyle::setLowColor(const QColor &v)         { if (m_lowColor      != v) { m_lowColor      = v; setDirty(); } }
void ContourBandStyle::setHighColor(const QColor &v)        { if (m_highColor     != v) { m_highColor     = v; setDirty(); } }
void ContourBandStyle::setBelowMinColor(const QColor &v)    { if (m_belowMinColor != v) { m_belowMinColor = v; setDirty(); } }
void ContourBandStyle::setAboveMaxColor(const QColor &v)    { if (m_aboveMaxColor != v) { m_aboveMaxColor = v; setDirty(); } }
void ContourBandStyle::setSmoothBands(bool v)               { if (m_smoothBands   != v) { m_smoothBands   = v; setDirty(); } }

QJsonObject ContourBandStyle::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("attribute"),     m_attribute);
    obj.insert(QStringLiteral("bandCount"),     m_bandCount);
    obj.insert(QStringLiteral("lowColor"),      m_lowColor.name(QColor::HexArgb));
    obj.insert(QStringLiteral("highColor"),     m_highColor.name(QColor::HexArgb));
    obj.insert(QStringLiteral("belowMinColor"), m_belowMinColor.name(QColor::HexArgb));
    obj.insert(QStringLiteral("aboveMaxColor"), m_aboveMaxColor.name(QColor::HexArgb));
    obj.insert(QStringLiteral("smoothBands"),   m_smoothBands);
    return obj;
}

void ContourBandStyle::fromJson(const QJsonObject &j)
{
    m_attribute   = j.value(QStringLiteral("attribute")).toString(m_attribute);
    m_bandCount   = j.value(QStringLiteral("bandCount")).toInt(m_bandCount);
    if (m_bandCount < 1) m_bandCount = 1;
    m_smoothBands = j.value(QStringLiteral("smoothBands")).toBool(m_smoothBands);

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
    QList<LegendSymbolItem> out;

    LegendSymbolItem lo;
    lo.label      = QStringLiteral("%1 band low").arg(m_style->attribute());
    lo.sublayerId = m_id;
    SymbolLayer loFill;
    loFill.kind = SymbolLayerKind::SimpleFill;
    loFill.props.insert(QStringLiteral("color"), m_style->lowColor().name(QColor::HexArgb));
    lo.symbol.layers.append(loFill);
    lo.symbol.opacity = m_opacity;
    out.append(lo);

    LegendSymbolItem hi;
    hi.label      = QStringLiteral("%1 band high").arg(m_style->attribute());
    hi.sublayerId = m_id;
    SymbolLayer hiFill;
    hiFill.kind = SymbolLayerKind::SimpleFill;
    hiFill.props.insert(QStringLiteral("color"), m_style->highColor().name(QColor::HexArgb));
    hi.symbol.layers.append(hiFill);
    hi.symbol.opacity = m_opacity;
    out.append(hi);

    return out;
}

QSGNode *ContourBandSublayer::buildOrUpdateNode(QSGNode *existing, const SublayerContext &ctx)
{
    Q_UNUSED(ctx);
    return existing;
}

} // namespace OpenSWMM::Render
