/*!
 * \file   rastersymbollayers.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Raster + TIN Symbol Layer specs (Slice Z.6).
 */

#include "render/rastersymbollayers.h"

#include "render/symbolstyle.h"

#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <array>

namespace OpenSWMM::Render
{

namespace {

// ── ContourMode mapping ──────────────────────────────────────────────

struct ContourModeMap { ContourMode m; const char *token; };
constexpr std::array<ContourModeMap, 3> kContourModes = {{
    {ContourMode::Lines,  "lines"},
    {ContourMode::Filled, "filled"},
    {ContourMode::Both,   "both"},
}};

// ── Helpers — embed/extract a typed object inside a QVariantMap ──────

QString jsonObjectToString(const QJsonObject &obj)
{
    return QString::fromUtf8(QJsonDocument(obj).toJson(QJsonDocument::Compact));
}

QJsonObject jsonObjectFromString(const QString &s)
{
    return QJsonDocument::fromJson(s.toUtf8()).object();
}

void writeRampToProps(QVariantMap &props, const QString &key,
                       const RasterColorRamp &r)
{
    props[key] = jsonObjectToString(r.toJson());
}

RasterColorRamp readRampFromProps(const QVariantMap &props,
                                    const QString &key,
                                    const RasterColorRamp &fallback)
{
    if (!props.contains(key))
        return fallback;
    const QJsonObject j = jsonObjectFromString(props.value(key).toString());
    if (j.isEmpty())
        return fallback;
    return RasterColorRamp::fromJson(j);
}

void writeBinnerToProps(QVariantMap &props, const QString &key,
                         const IntervalBinner &b)
{
    props[key] = jsonObjectToString(b.toJson());
}

IntervalBinner readBinnerFromProps(const QVariantMap &props,
                                     const QString &key,
                                     const IntervalBinner &fallback)
{
    if (!props.contains(key))
        return fallback;
    const QJsonObject j = jsonObjectFromString(props.value(key).toString());
    if (j.isEmpty())
        return fallback;
    return IntervalBinner::fromJson(j);
}

} // namespace

// ╭───────────────────────────────────────────────────────────────────╮
// │  ContourMode strings                                                │
// ╰───────────────────────────────────────────────────────────────────╯

QString contourModeToString(ContourMode m)
{
    for (const auto &x : kContourModes)
        if (x.m == m) return QString::fromLatin1(x.token);
    return QStringLiteral("lines");
}

ContourMode contourModeFromString(const QString &s)
{
    for (const auto &x : kContourModes)
        if (s == QLatin1String(x.token)) return x.m;
    return ContourMode::Lines;
}

// ╭───────────────────────────────────────────────────────────────────╮
// │  RasterColorRampSymbolLayerSpec                                     │
// ╰───────────────────────────────────────────────────────────────────╯

SymbolLayer RasterColorRampSymbolLayerSpec::toSymbolLayer() const
{
    SymbolLayer layer;
    writeToSymbolLayer(layer);
    return layer;
}

RasterColorRampSymbolLayerSpec
RasterColorRampSymbolLayerSpec::fromSymbolLayer(const SymbolLayer &layer)
{
    RasterColorRampSymbolLayerSpec s;
    const QVariantMap &p = layer.props;

    s.ramp        = readRampFromProps(p, QStringLiteral("ramp"), s.ramp);
    s.binner      = readBinnerFromProps(p, QStringLiteral("binner"), s.binner);
    if (p.contains(QStringLiteral("clampMin")))
        s.clampMin = p.value(QStringLiteral("clampMin")).toDouble();
    if (p.contains(QStringLiteral("clampMax")))
        s.clampMax = p.value(QStringLiteral("clampMax")).toDouble();
    // Gap A1.2 — tolerant read (QColor variant or legacy hex string).
    s.noDataColor = SymbolProps::readColor(p, QStringLiteral("noDataColor"),
                                           s.noDataColor);
    if (p.contains(QStringLiteral("opacity")))
        s.opacity = std::clamp(p.value(QStringLiteral("opacity")).toDouble(),
                               0.0, 1.0);
    return s;
}

void RasterColorRampSymbolLayerSpec::writeToSymbolLayer(SymbolLayer &layer) const
{
    layer.kind = SymbolLayerKind::RasterColorRamp;
    writeRampToProps(layer.props, QStringLiteral("ramp"), ramp);
    writeBinnerToProps(layer.props, QStringLiteral("binner"), binner);
    layer.props[QStringLiteral("clampMin")]    = clampMin;
    layer.props[QStringLiteral("clampMax")]    = clampMax;
    layer.props[QStringLiteral("noDataColor")] = noDataColor;
    layer.props[QStringLiteral("opacity")]     = std::clamp(opacity, 0.0, 1.0);
}

// ╭───────────────────────────────────────────────────────────────────╮
// │  HillshadeSymbolLayerSpec                                           │
// ╰───────────────────────────────────────────────────────────────────╯

SymbolLayer HillshadeSymbolLayerSpec::toSymbolLayer() const
{
    SymbolLayer layer;
    writeToSymbolLayer(layer);
    return layer;
}

HillshadeSymbolLayerSpec
HillshadeSymbolLayerSpec::fromSymbolLayer(const SymbolLayer &layer)
{
    HillshadeSymbolLayerSpec s;
    const QVariantMap &p = layer.props;

    if (p.contains(QStringLiteral("azimuthDeg")))
        s.azimuthDeg = p.value(QStringLiteral("azimuthDeg")).toDouble();
    if (p.contains(QStringLiteral("altitudeDeg")))
        s.altitudeDeg = p.value(QStringLiteral("altitudeDeg")).toDouble();
    if (p.contains(QStringLiteral("zExaggeration")))
        s.zExaggeration = p.value(QStringLiteral("zExaggeration")).toDouble();
    if (p.contains(QStringLiteral("shadowFloor")))
        s.shadowFloor = std::clamp(
            p.value(QStringLiteral("shadowFloor")).toDouble(), 0.0, 1.0);
    if (p.contains(QStringLiteral("blendMode"))) {
        const QString b = p.value(QStringLiteral("blendMode")).toString();
        if (!b.isEmpty()) s.blendMode = b;
    }
    // Slice Z.6a — hillshade strength multiplier.
    if (p.contains(QStringLiteral("strength")))
        s.strength = std::clamp(
            p.value(QStringLiteral("strength")).toDouble(), 0.0, 1.0);
    return s;
}

void HillshadeSymbolLayerSpec::writeToSymbolLayer(SymbolLayer &layer) const
{
    layer.kind = SymbolLayerKind::Hillshade;
    layer.props[QStringLiteral("azimuthDeg")]    = azimuthDeg;
    layer.props[QStringLiteral("altitudeDeg")]   = altitudeDeg;
    layer.props[QStringLiteral("zExaggeration")] = zExaggeration;
    layer.props[QStringLiteral("shadowFloor")]   = std::clamp(shadowFloor, 0.0, 1.0);
    layer.props[QStringLiteral("blendMode")]     = blendMode;
    layer.props[QStringLiteral("strength")]      = std::clamp(strength, 0.0, 1.0);
}

// ╭───────────────────────────────────────────────────────────────────╮
// │  ContourSymbolLayerSpec                                             │
// ╰───────────────────────────────────────────────────────────────────╯

SymbolLayer ContourSymbolLayerSpec::toSymbolLayer() const
{
    SymbolLayer layer;
    writeToSymbolLayer(layer);
    return layer;
}

ContourSymbolLayerSpec
ContourSymbolLayerSpec::fromSymbolLayer(const SymbolLayer &layer)
{
    ContourSymbolLayerSpec s;
    const QVariantMap &p = layer.props;

    if (p.contains(QStringLiteral("mode")))
        s.mode = static_cast<ContourMode>(
            p.value(QStringLiteral("mode")).toInt());
    s.binner = readBinnerFromProps(p, QStringLiteral("binner"), s.binner);
    s.ramp   = readRampFromProps(p, QStringLiteral("ramp"), s.ramp);
    s.lineColor = SymbolProps::readColor(p, QStringLiteral("lineColor"),
                                         s.lineColor);
    if (p.contains(QStringLiteral("lineWidthPx")))
        s.lineWidthPx = p.value(QStringLiteral("lineWidthPx")).toDouble();
    if (p.contains(QStringLiteral("labelEveryN")))
        s.labelEveryN = p.value(QStringLiteral("labelEveryN")).toInt();
    if (p.contains(QStringLiteral("labelFormat"))) {
        const QString f = p.value(QStringLiteral("labelFormat")).toString();
        if (!f.isEmpty()) s.labelFormat = f;
    }
    // Slice Z.6a — smooth-vs-categorical band toggle.
    if (p.contains(QStringLiteral("smoothBands")))
        s.smoothBands = p.value(QStringLiteral("smoothBands")).toBool();
    return s;
}

void ContourSymbolLayerSpec::writeToSymbolLayer(SymbolLayer &layer) const
{
    layer.kind = SymbolLayerKind::Contour;
    layer.props[QStringLiteral("mode")]        = static_cast<int>(mode);
    writeBinnerToProps(layer.props, QStringLiteral("binner"), binner);
    writeRampToProps(layer.props, QStringLiteral("ramp"), ramp);
    layer.props[QStringLiteral("lineColor")]   = lineColor;
    layer.props[QStringLiteral("lineWidthPx")] = lineWidthPx;
    layer.props[QStringLiteral("labelEveryN")] = labelEveryN;
    layer.props[QStringLiteral("labelFormat")] = labelFormat;
    layer.props[QStringLiteral("smoothBands")] = smoothBands;
}

// ╭───────────────────────────────────────────────────────────────────╮
// │  MeshEdgeSymbolLayerSpec                                            │
// ╰───────────────────────────────────────────────────────────────────╯

QPen MeshEdgeSymbolLayerSpec::toQPen() const
{
    QPen pen(color);
    pen.setWidthF(width);
    pen.setStyle(penStyle);
    pen.setCosmetic(false);
    return pen;
}

SymbolLayer MeshEdgeSymbolLayerSpec::toSymbolLayer() const
{
    SymbolLayer layer;
    writeToSymbolLayer(layer);
    return layer;
}

MeshEdgeSymbolLayerSpec
MeshEdgeSymbolLayerSpec::fromSymbolLayer(const SymbolLayer &layer)
{
    MeshEdgeSymbolLayerSpec s;
    const QVariantMap &p = layer.props;

    s.color = SymbolProps::readColor(p, QStringLiteral("color"), s.color);
    if (p.contains(QStringLiteral("width")))
        s.width = p.value(QStringLiteral("width")).toDouble();
    if (p.contains(QStringLiteral("penStyle")))
        s.penStyle = static_cast<Qt::PenStyle>(
            p.value(QStringLiteral("penStyle")).toInt());
    if (p.contains(QStringLiteral("lodMinZoom")))
        s.lodMinZoom = p.value(QStringLiteral("lodMinZoom")).toInt();
    // Slice Z.6a — slope-driven width fields.
    if (p.contains(QStringLiteral("useSlopeDrivenWidth")))
        s.useSlopeDrivenWidth = p.value(QStringLiteral("useSlopeDrivenWidth")).toBool();
    if (p.contains(QStringLiteral("slopeBreak")))
        s.slopeBreak = p.value(QStringLiteral("slopeBreak")).toDouble();
    if (p.contains(QStringLiteral("wideWidthPx")))
        s.wideWidthPx = p.value(QStringLiteral("wideWidthPx")).toDouble();
    s.wideColor = SymbolProps::readColor(p, QStringLiteral("wideColor"),
                                         s.wideColor);
    return s;
}

void MeshEdgeSymbolLayerSpec::writeToSymbolLayer(SymbolLayer &layer) const
{
    layer.kind = SymbolLayerKind::MeshEdge;
    layer.props[QStringLiteral("color")]      = color;
    layer.props[QStringLiteral("width")]      = width;
    layer.props[QStringLiteral("penStyle")]   = static_cast<int>(penStyle);
    layer.props[QStringLiteral("lodMinZoom")] = lodMinZoom;
    // Slice Z.6a — slope-driven width fields.
    layer.props[QStringLiteral("useSlopeDrivenWidth")] = useSlopeDrivenWidth;
    layer.props[QStringLiteral("slopeBreak")]          = slopeBreak;
    layer.props[QStringLiteral("wideWidthPx")]         = wideWidthPx;
    layer.props[QStringLiteral("wideColor")]           = wideColor;
}

// ╭───────────────────────────────────────────────────────────────────╮
// │  MeshNodeSymbolLayerSpec                                            │
// ╰───────────────────────────────────────────────────────────────────╯

SymbolLayer MeshNodeSymbolLayerSpec::toSymbolLayer() const
{
    SymbolLayer layer;
    writeToSymbolLayer(layer);
    return layer;
}

MeshNodeSymbolLayerSpec
MeshNodeSymbolLayerSpec::fromSymbolLayer(const SymbolLayer &layer)
{
    MeshNodeSymbolLayerSpec s;
    s.marker = MarkerSymbolLayerSpec::fromSymbolLayer(layer);
    return s;
}

void MeshNodeSymbolLayerSpec::writeToSymbolLayer(SymbolLayer &layer) const
{
    // Reuse the Marker writer (same canonical props), then override kind.
    marker.writeToSymbolLayer(layer);
    layer.kind = SymbolLayerKind::MeshNode;
}

// ╭───────────────────────────────────────────────────────────────────╮
// │  VelocityVectorSymbolLayerSpec (Slice AN.1)                         │
// ╰───────────────────────────────────────────────────────────────────╯

SymbolLayer VelocityVectorSymbolLayerSpec::toSymbolLayer() const
{
    SymbolLayer layer;
    writeToSymbolLayer(layer);
    return layer;
}

VelocityVectorSymbolLayerSpec
VelocityVectorSymbolLayerSpec::fromSymbolLayer(const SymbolLayer &layer)
{
    VelocityVectorSymbolLayerSpec s;
    const QVariantMap &p = layer.props;

    if (p.contains(QStringLiteral("glyphLengthScalePxPerMps")))
        s.glyphLengthScalePxPerMps =
            p.value(QStringLiteral("glyphLengthScalePxPerMps")).toDouble();
    if (p.contains(QStringLiteral("glyphLengthMinPx")))
        s.glyphLengthMinPx = p.value(QStringLiteral("glyphLengthMinPx")).toDouble();
    if (p.contains(QStringLiteral("glyphLengthMaxPx")))
        s.glyphLengthMaxPx = p.value(QStringLiteral("glyphLengthMaxPx")).toDouble();
    if (p.contains(QStringLiteral("glyphSpacingPx")))
        s.glyphSpacingPx = p.value(QStringLiteral("glyphSpacingPx")).toDouble();
    if (p.contains(QStringLiteral("headSizePx")))
        s.headSizePx = p.value(QStringLiteral("headSizePx")).toDouble();
    s.color = SymbolProps::readColor(p, QStringLiteral("color"), s.color);
    if (p.contains(QStringLiteral("dryDepthCutoff")))
        s.dryDepthCutoff = p.value(QStringLiteral("dryDepthCutoff")).toDouble();

    return s;
}

void VelocityVectorSymbolLayerSpec::writeToSymbolLayer(SymbolLayer &layer) const
{
    layer.kind = SymbolLayerKind::VectorGlyph;
    layer.props[QStringLiteral("glyphLengthScalePxPerMps")] = glyphLengthScalePxPerMps;
    layer.props[QStringLiteral("glyphLengthMinPx")]         = glyphLengthMinPx;
    layer.props[QStringLiteral("glyphLengthMaxPx")]         = glyphLengthMaxPx;
    layer.props[QStringLiteral("glyphSpacingPx")]           = glyphSpacingPx;
    layer.props[QStringLiteral("headSizePx")]               = headSizePx;
    layer.props[QStringLiteral("color")]                    = color;
    layer.props[QStringLiteral("dryDepthCutoff")]           = dryDepthCutoff;
}

} // namespace OpenSWMM::Render
