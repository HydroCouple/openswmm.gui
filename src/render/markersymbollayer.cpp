/*!
 * \file   markersymbollayer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  MarkerSymbolLayerSpec implementation (Slice Z.4).
 */

#include "render/markersymbollayer.h"

#include "render/symbolstyle.h"

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
    // Gap A1.2 — tolerant reads (QColor variant or legacy hex string).
    s.fillColor    = SymbolProps::readColor(p, QStringLiteral("fillColor"),
                                            s.fillColor);
    s.outlineColor = SymbolProps::readColor(p, QStringLiteral("outlineColor"),
                                            s.outlineColor);
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
    s.labelColor = SymbolProps::readColor(p, QStringLiteral("labelColor"),
                                          s.labelColor);

    return s;
}

void MarkerSymbolLayerSpec::writeToSymbolLayer(SymbolLayer &layer) const
{
    layer.kind = SymbolLayerKind::SimpleMarker;
    layer.props[QStringLiteral("shape")]           = static_cast<int>(shape);
    layer.props[QStringLiteral("size")]            = sizePx;
    // NOTE: colours are stored as QColor variants (NOT hex strings). The
    // readers here and across the symbol-style stack use QVariant::value<QColor>(),
    // which does NOT parse a #AARRGGBB QString in this codebase — storing hex
    // made every colour read back invalid and reset to defaults (X1 regression,
    // reverted 2026-06-01).
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
