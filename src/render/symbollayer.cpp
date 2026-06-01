/*!
 * \file   symbollayer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  JSON round-trip + kind-string mapping for SymbolLayer.
 */

#include "render/symbollayer.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>

namespace OpenSWMM::Render
{

QString symbolLayerKindToString(SymbolLayerKind kind)
{
    switch (kind)
    {
    case SymbolLayerKind::SimpleMarker:    return QStringLiteral("simpleMarker");
    case SymbolLayerKind::SimpleLine:      return QStringLiteral("simpleLine");
    case SymbolLayerKind::SimpleFill:      return QStringLiteral("simpleFill");
    case SymbolLayerKind::MarkerLine:      return QStringLiteral("markerLine");
    case SymbolLayerKind::HatchFill:       return QStringLiteral("hatchFill");
    case SymbolLayerKind::PatternFill:     return QStringLiteral("patternFill");
    case SymbolLayerKind::SvgMarker:       return QStringLiteral("svgMarker");
    case SymbolLayerKind::FontMarker:      return QStringLiteral("fontMarker");
    // Slice Z.6 — Raster + TIN paint passes.
    case SymbolLayerKind::RasterColorRamp: return QStringLiteral("rasterColorRamp");
    case SymbolLayerKind::Hillshade:       return QStringLiteral("hillshade");
    case SymbolLayerKind::Contour:         return QStringLiteral("contour");
    case SymbolLayerKind::MeshEdge:        return QStringLiteral("meshEdge");
    case SymbolLayerKind::MeshNode:        return QStringLiteral("meshNode");
    // Slice AN.1 — 2D velocity-glyph kind.
    case SymbolLayerKind::VectorGlyph:     return QStringLiteral("vectorGlyph");
    }
    return QStringLiteral("simpleMarker");
}

SymbolLayerKind symbolLayerKindFromString(const QString &s)
{
    if (s == QLatin1String("simpleMarker"))    return SymbolLayerKind::SimpleMarker;
    if (s == QLatin1String("simpleLine"))      return SymbolLayerKind::SimpleLine;
    if (s == QLatin1String("simpleFill"))      return SymbolLayerKind::SimpleFill;
    if (s == QLatin1String("markerLine"))      return SymbolLayerKind::MarkerLine;
    if (s == QLatin1String("hatchFill"))       return SymbolLayerKind::HatchFill;
    if (s == QLatin1String("patternFill"))     return SymbolLayerKind::PatternFill;
    if (s == QLatin1String("svgMarker"))       return SymbolLayerKind::SvgMarker;
    if (s == QLatin1String("fontMarker"))      return SymbolLayerKind::FontMarker;
    // Slice Z.6.
    if (s == QLatin1String("rasterColorRamp")) return SymbolLayerKind::RasterColorRamp;
    if (s == QLatin1String("hillshade"))       return SymbolLayerKind::Hillshade;
    if (s == QLatin1String("contour"))         return SymbolLayerKind::Contour;
    if (s == QLatin1String("meshEdge"))        return SymbolLayerKind::MeshEdge;
    if (s == QLatin1String("meshNode"))        return SymbolLayerKind::MeshNode;
    // Slice AN.1.
    if (s == QLatin1String("vectorGlyph"))     return SymbolLayerKind::VectorGlyph;
    // Unknown / future kind — round-trip safely by falling back.
    return SymbolLayerKind::SimpleMarker;
}

QJsonObject SymbolLayer::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("kind"), symbolLayerKindToString(kind));
    obj.insert(QStringLiteral("props"), QJsonObject::fromVariantMap(props));

    if (!dataDefinedOverrides.isEmpty())
    {
        QJsonObject ddo;
        for (auto it = dataDefinedOverrides.constBegin();
             it != dataDefinedOverrides.constEnd(); ++it)
        {
            ddo.insert(it.key(), it.value());
        }
        obj.insert(QStringLiteral("dataDefinedOverrides"), ddo);
    }
    return obj;
}

void SymbolLayer::fromJson(const QJsonObject &j)
{
    kind = symbolLayerKindFromString(j.value(QStringLiteral("kind")).toString());
    props = j.value(QStringLiteral("props")).toObject().toVariantMap();

    dataDefinedOverrides.clear();
    const QJsonObject ddo = j.value(QStringLiteral("dataDefinedOverrides")).toObject();
    for (auto it = ddo.constBegin(); it != ddo.constEnd(); ++it)
    {
        dataDefinedOverrides.insert(it.key(), it.value().toString());
    }
}

} // namespace OpenSWMM::Render
