/*!
 * \file   symbollayer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  JSON round-trip + kind-string mapping for SymbolLayer.
 */

#include "render/symbollayer.h"

#include <QColor>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QStringList>

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

namespace {

// X1 — colour props are QColor *variants* in memory (the typed spec readers
// use QVariant::value<QColor>(), which does not parse hex strings), but
// QJsonObject::fromVariantMap silently drops QColor variants (they have no
// JSON mapping). Convert colours to hex at the JSON boundary on save and
// back to QColor variants on load so both the readers and persistence work.
//
// Gap A1.2 — key *suffix* heuristic instead of a fixed list: the old list
// missed the raster grammar keys (noDataColor / lineColor / wideColor), so
// raster symbol colours stayed hex strings after load and every
// value<QColor>() read silently fell back to defaults.
bool isColorPropKey(const QString &key)
{
    return key == QLatin1String("color") || key.endsWith(QLatin1String("Color"));
}

} // namespace

QJsonObject SymbolLayer::toJson() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("kind"), symbolLayerKindToString(kind));
    QVariantMap jsonProps = props;
    for (auto it = jsonProps.begin(); it != jsonProps.end(); ++it) {
        if (it.value().userType() == QMetaType::QColor)
            it.value() = it.value().value<QColor>().name(QColor::HexArgb);
    }
    obj.insert(QStringLiteral("props"), QJsonObject::fromVariantMap(jsonProps));

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
    // X1 — rehydrate colour keys saved as hex strings into QColor variants
    // (the in-memory canonical form; see toJson).
    for (auto it = props.begin(); it != props.end(); ++it) {
        if (!isColorPropKey(it.key())) continue;
        if (it.value().userType() == QMetaType::QColor) continue;
        const QColor c(it.value().toString());
        if (c.isValid()) it.value() = QVariant::fromValue(c);
    }

    dataDefinedOverrides.clear();
    const QJsonObject ddo = j.value(QStringLiteral("dataDefinedOverrides")).toObject();
    for (auto it = ddo.constBegin(); it != ddo.constEnd(); ++it)
    {
        dataDefinedOverrides.insert(it.key(), it.value().toString());
    }
}

} // namespace OpenSWMM::Render
