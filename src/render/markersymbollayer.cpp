/*!
 * \file   markersymbollayer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  MarkerSymbolLayerSpec implementation (Slice Z.4).
 */

#include "render/markersymbollayer.h"

namespace OpenSWMM::Render
{

QPen MarkerSymbolLayerSpec::outlinePen() const
{
    QPen pen(outlineColor);
    pen.setWidthF(outlineWidth);
    pen.setStyle(outlinePenStyle);
    pen.setCosmetic(false);
    return pen;
}

SymbolLayer MarkerSymbolLayerSpec::toSymbolLayer() const
{
    SymbolLayer layer;
    writeToSymbolLayer(layer);
    return layer;
}

MarkerSymbolLayerSpec MarkerSymbolLayerSpec::fromSymbolLayer(const SymbolLayer &layer)
{
    MarkerSymbolLayerSpec s;
    const QVariantMap &p = layer.props;

    if (p.contains(QStringLiteral("shape")))
        s.shape = static_cast<MarkerShape>(
            p.value(QStringLiteral("shape")).toInt());
    if (p.contains(QStringLiteral("size")))
        s.sizePx = p.value(QStringLiteral("size")).toDouble();
    if (p.contains(QStringLiteral("fillColor"))) {
        const QColor c = p.value(QStringLiteral("fillColor")).value<QColor>();
        if (c.isValid()) s.fillColor = c;
    }
    if (p.contains(QStringLiteral("outlineColor"))) {
        const QColor c = p.value(QStringLiteral("outlineColor")).value<QColor>();
        if (c.isValid()) s.outlineColor = c;
    }
    if (p.contains(QStringLiteral("outlineWidth")))
        s.outlineWidth = p.value(QStringLiteral("outlineWidth")).toDouble();
    if (p.contains(QStringLiteral("outlinePenStyle")))
        s.outlinePenStyle = static_cast<Qt::PenStyle>(
            p.value(QStringLiteral("outlinePenStyle")).toInt());
    if (p.contains(QStringLiteral("rotationDeg")))
        s.rotationDeg = p.value(QStringLiteral("rotationDeg")).toDouble();
    if (p.contains(QStringLiteral("offsetX")))
        s.offsetPx.setX(p.value(QStringLiteral("offsetX")).toDouble());
    if (p.contains(QStringLiteral("offsetY")))
        s.offsetPx.setY(p.value(QStringLiteral("offsetY")).toDouble());

    // Slice SS.3 — label fields.
    if (p.contains(QStringLiteral("showLabel")))
        s.showLabel = p.value(QStringLiteral("showLabel")).toBool();
    if (p.contains(QStringLiteral("labelFont"))) {
        const QFont f = p.value(QStringLiteral("labelFont")).value<QFont>();
        s.labelFont = f;
    }
    if (p.contains(QStringLiteral("labelColor"))) {
        const QColor c = p.value(QStringLiteral("labelColor")).value<QColor>();
        if (c.isValid()) s.labelColor = c;
    }

    return s;
}

void MarkerSymbolLayerSpec::writeToSymbolLayer(SymbolLayer &layer) const
{
    layer.kind = SymbolLayerKind::SimpleMarker;
    layer.props[QStringLiteral("shape")]           = static_cast<int>(shape);
    layer.props[QStringLiteral("size")]            = sizePx;
    layer.props[QStringLiteral("fillColor")]       = fillColor;
    layer.props[QStringLiteral("outlineColor")]    = outlineColor;
    layer.props[QStringLiteral("outlineWidth")]    = outlineWidth;
    layer.props[QStringLiteral("outlinePenStyle")] = static_cast<int>(outlinePenStyle);
    layer.props[QStringLiteral("rotationDeg")]     = rotationDeg;
    layer.props[QStringLiteral("offsetX")]         = offsetPx.x();
    layer.props[QStringLiteral("offsetY")]         = offsetPx.y();
    // Slice SS.3 — label fields parallel to SWMMElementSymbol.
    layer.props[QStringLiteral("showLabel")]       = showLabel;
    layer.props[QStringLiteral("labelFont")]       = labelFont;
    layer.props[QStringLiteral("labelColor")]      = labelColor;
}

} // namespace OpenSWMM::Render
