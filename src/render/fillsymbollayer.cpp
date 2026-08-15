/*!
 * \file   fillsymbollayer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  FillSymbolLayerSpec + fill draw helper (VS.2).
 */

#include "render/fillsymbollayer.h"

#include "render/symbolstyle.h"

#include <QPainter>

namespace OpenSWMM::Render
{

QBrush FillSymbolLayerSpec::toQBrush() const
{
    if (fillStyle == Qt::NoBrush)
        return QBrush(Qt::NoBrush);
    return QBrush(fillColor, fillStyle);
}

QPen FillSymbolLayerSpec::toQPen() const
{
    if (outlinePenStyle == Qt::NoPen || outlineWidth <= 0.0)
        return QPen(Qt::NoPen);
    QPen pen(outlineColor);
    pen.setWidthF(outlineWidth);
    pen.setStyle(outlinePenStyle);
    pen.setJoinStyle(joinStyle);
    pen.setCosmetic(false);
    return pen;
}

SymbolLayer FillSymbolLayerSpec::toSymbolLayer() const
{
    SymbolLayer layer;
    writeToSymbolLayer(layer);
    return layer;
}

FillSymbolLayerSpec FillSymbolLayerSpec::fromSymbolLayer(const SymbolLayer &layer)
{
    FillSymbolLayerSpec s;
    const QVariantMap &p = layer.props;

    // Gap A1.2 — tolerant reads (QColor variant or legacy hex string).
    s.fillColor = SymbolProps::readColor(p, QStringLiteral("fillColor"),
                                         s.fillColor);
    if (p.contains(QStringLiteral("fillStyle")))
        s.fillStyle = static_cast<Qt::BrushStyle>(
            p.value(QStringLiteral("fillStyle")).toInt());
    s.outlineColor = SymbolProps::readColor(p, QStringLiteral("outlineColor"),
                                            s.outlineColor);
    if (p.contains(QStringLiteral("outlineWidth")))
        s.outlineWidth = p.value(QStringLiteral("outlineWidth")).toDouble();
    if (p.contains(QStringLiteral("outlinePenStyle")))
        s.outlinePenStyle = static_cast<Qt::PenStyle>(
            p.value(QStringLiteral("outlinePenStyle")).toInt());
    if (p.contains(QStringLiteral("joinStyle")))
        s.joinStyle = static_cast<Qt::PenJoinStyle>(
            p.value(QStringLiteral("joinStyle")).toInt());

    return s;
}

void FillSymbolLayerSpec::writeToSymbolLayer(SymbolLayer &layer) const
{
    layer.kind = SymbolLayerKind::SimpleFill;
    // QColor variants (hex reverted — readers use value<QColor>() which doesn't
    // parse hex strings here; X1 regression fix 2026-06-01).
    layer.props[QStringLiteral("fillColor")]       = fillColor;
    layer.props[QStringLiteral("fillStyle")]       = static_cast<int>(fillStyle);
    layer.props[QStringLiteral("outlineColor")]    = outlineColor;
    layer.props[QStringLiteral("outlineWidth")]    = outlineWidth;
    layer.props[QStringLiteral("outlinePenStyle")] = static_cast<int>(outlinePenStyle);
    layer.props[QStringLiteral("joinStyle")]       = static_cast<int>(joinStyle);
}

void drawFill(QPainter *painter,
              const QPolygonF &polygon,
              const FillSymbolLayerSpec &spec)
{
    if (!painter || polygon.size() < 3)
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setBrush(spec.toQBrush());
    painter->setPen(spec.toQPen());
    painter->drawPolygon(polygon);
    painter->restore();
}

} // namespace OpenSWMM::Render
