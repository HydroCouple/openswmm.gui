/**
 * @file coupledcellsublayer.cpp
 * @brief Appearance and legend for explicit cell couplings.
 * @license GPL-3.0-or-later
 */
#include "render/sublayers/coupledcellsublayer.h"
#include <cmath>

namespace OpenSWMM::Render
{
void CoupledCellStyle::setFillColor(const QColor &color)
{
    if (!color.isValid() || color == m_fill)
        return;
    m_fill = color;
    setDirty();
}

void CoupledCellStyle::setOutlineColor(const QColor &color)
{
    if (!color.isValid() || color == m_outline)
        return;
    m_outline = color;
    setDirty();
}

void CoupledCellStyle::setOutlineWidthPx(double width)
{
    if (!std::isfinite(width))
        return;
    width = qBound(0.5, width, 10.0);
    if (qFuzzyCompare(width, m_width))
        return;
    m_width = width;
    setDirty();
}

QJsonObject CoupledCellStyle::toJson() const
{
    return {{"fillColor", m_fill.name(QColor::HexArgb)},
            {"outlineColor", m_outline.name(QColor::HexArgb)},
            {"outlineWidthPx", m_width}};
}

void CoupledCellStyle::fromJson(const QJsonObject &json)
{
    if (json.contains("fillColor"))
        setFillColor(QColor(json.value("fillColor").toString()));
    if (json.contains("outlineColor"))
        setOutlineColor(QColor(json.value("outlineColor").toString()));
    if (json.contains("outlineWidthPx"))
        setOutlineWidthPx(json.value("outlineWidthPx").toDouble(m_width));
}

CoupledCellSublayer::CoupledCellSublayer(QString id, QObject *parent)
    : ISublayer(parent), m_id(std::move(id)), m_style(new CoupledCellStyle(this))
{
    connect(m_style, &SublayerStyle::styleChanged, this, &ISublayer::invalidated);
}

void CoupledCellSublayer::setVisible(bool visible)
{
    if (m_visible == visible)
        return;
    m_visible = visible;
    emit invalidated();
}

void CoupledCellSublayer::setOpacity(qreal opacity)
{
    if (!std::isfinite(opacity))
        return;
    opacity = qBound(0.0, opacity, 1.0);
    if (qFuzzyCompare(m_opacity + 1.0, opacity + 1.0))
        return;
    m_opacity = opacity;
    emit invalidated();
}

QList<LegendSymbolItem> CoupledCellSublayer::legendSymbolItems() const
{
    LegendSymbolItem item;
    item.label = tr("Cells coupled to 1D nodes");
    item.sublayerId = m_id;
    SymbolLayer fill;
    fill.kind = SymbolLayerKind::SimpleFill;
    SymbolProps::writeColor(fill.props, QStringLiteral("color"), m_style->fillColor());
    SymbolProps::writeColor(fill.props, QStringLiteral("outlineColor"), m_style->outlineColor());
    fill.props.insert(QStringLiteral("outlineWidth"), m_style->outlineWidthPx());
    item.symbol.layers.append(fill);
    item.symbol.opacity = m_opacity;
    return {item};
}
} // namespace OpenSWMM::Render
