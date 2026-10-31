/**
 * @file      groundwatermarkersublayer.cpp
 * @brief     R7 groundwater source / aquifer node-bed marker sublayer.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include "render/sublayers/groundwatermarkersublayer.h"

#include <QJsonObject>

namespace OpenSWMM::Render
{
void GroundwaterMarkerStyle::setSourceColor(const QColor &v)
{
    if (m_sourceColor == v)
        return;
    m_sourceColor = v;
    setDirty();
}

void GroundwaterMarkerStyle::setBedColor(const QColor &v)
{
    if (m_bedColor == v)
        return;
    m_bedColor = v;
    setDirty();
}

void GroundwaterMarkerStyle::setNoExchangeColor(const QColor &v)
{
    if (m_noExchangeColor == v)
        return;
    m_noExchangeColor = v;
    setDirty();
}

void GroundwaterMarkerStyle::setMarkerSizePx(double v)
{
    v = qBound(2.0, v, 40.0);
    if (qFuzzyCompare(m_markerSizePx + 1.0, v + 1.0))
        return;
    m_markerSizePx = v;
    setDirty();
}

QJsonObject GroundwaterMarkerStyle::toJson() const
{
    QJsonObject j;
    j[QStringLiteral("sourceColor")] = m_sourceColor.name(QColor::HexArgb);
    j[QStringLiteral("bedColor")] = m_bedColor.name(QColor::HexArgb);
    j[QStringLiteral("noExchangeColor")] = m_noExchangeColor.name(QColor::HexArgb);
    j[QStringLiteral("markerSizePx")] = m_markerSizePx;
    return j;
}

void GroundwaterMarkerStyle::fromJson(const QJsonObject &j)
{
    const auto colour = [&j](const char *key, QColor &field) {
        const QColor c(j.value(QLatin1String(key)).toString());
        if (c.isValid())
            field = c;
    };
    colour("sourceColor", m_sourceColor);
    colour("bedColor", m_bedColor);
    colour("noExchangeColor", m_noExchangeColor);
    if (j.contains(QStringLiteral("markerSizePx")))
        m_markerSizePx =
            qBound(2.0, j.value(QStringLiteral("markerSizePx")).toDouble(m_markerSizePx), 40.0);
    setDirty();
}

GroundwaterMarkerSublayer::GroundwaterMarkerSublayer(QString id_, QObject *parent)
    : ISublayer(parent), m_id(std::move(id_)), m_style(new GroundwaterMarkerStyle(this))
{
    connect(m_style, &SublayerStyle::styleChanged, this, &ISublayer::invalidated);
}

void GroundwaterMarkerSublayer::setVisible(bool v)
{
    if (m_visible == v)
        return;
    m_visible = v;
    emit invalidated();
}

void GroundwaterMarkerSublayer::setOpacity(qreal o)
{
    o = qBound(0.0, o, 1.0);
    if (qFuzzyCompare(m_opacity + 1.0, o + 1.0))
        return;
    m_opacity = o;
    emit invalidated();
}

QList<LegendSymbolItem> GroundwaterMarkerSublayer::legendSymbolItems() const
{
    const std::pair<QString, QColor> rows[] = {
        {tr("Groundwater source"), m_style->sourceColor()},
        {tr("Aquifer node bed"), m_style->bedColor()},
        {tr("Aquifer node bed (exchange off)"), m_style->noExchangeColor()},
    };
    QList<LegendSymbolItem> out;
    int index = 0;
    for (const auto &[label, colour] : rows) {
        LegendSymbolItem item;
        item.label = label;
        item.sublayerId = m_id;
        item.sortIndex = index++;
        SymbolLayer marker;
        marker.kind = SymbolLayerKind::SimpleMarker;
        SymbolProps::writeColor(marker.props, QStringLiteral("color"), colour);
        marker.props.insert(QStringLiteral("sizePx"), m_style->markerSizePx());
        item.symbol.layers.append(marker);
        item.symbol.opacity = m_opacity;
        out.append(item);
    }
    return out;
}

QSGNode *GroundwaterMarkerSublayer::buildOrUpdateNode(QSGNode *existing, const SublayerContext &ctx)
{
    Q_UNUSED(ctx);
    return existing; // QSG geometry is built in SWMM2DMeshQSGRenderer.
}

} // namespace OpenSWMM::Render
