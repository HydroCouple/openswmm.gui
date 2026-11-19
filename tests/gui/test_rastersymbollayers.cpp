/**
 * @file      test_rastersymbollayers.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Tests for Slice Z.6 — Raster + TIN Symbol Layers.
 *
 *         Success criterion (RENDERING_RULE_MODEL_PLAN.md §16, Z.6):
 *           - All 5 new SymbolLayerKind values round-trip through
 *             string.
 *           - Each spec's toSymbolLayer/fromSymbolLayer is lossless on
 *             every typed field.
 *           - ColorRamp + IntervalBinner embedded in props round-trip
 *             through Z.6's per-key encoding.
 *           - Partial props fall back to spec defaults.
 *           - MeshNode delegates to MarkerSymbolLayerSpec without
 *             corrupting marker fields.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include <QColor>
#include <QtTest/QtTest>

#include "render/markersymbollayer.h"
#include "render/rastersymbollayers.h"
#include "render/symbollayer.h"

using namespace OpenSWMM::Render;

class TestRasterSymbolLayers : public QObject
{
    Q_OBJECT

private slots:
    // Enum
    /**
     *
     * @brief Require five raster/TIN symbol kinds to have nonempty reversible string encodings.
     *
     */
    void enum_newKindsRoundTripThroughString();
    /**
     *
     * @brief Require three contour modes to round-trip strings and an unknown token to fall back
     * to Lines.
     *
     */
    void enum_contourModeRoundTrips();

    // RasterColorRampSymbolLayerSpec
    /**
     *
     * @brief Require initial opacity within [0,1] and ordered clamp bounds.
     *
     */
    void rasterRamp_defaultsAreSensible();
    /**
     *
     * @brief Require RasterColorRamp symbol kind from its typed specification.
     *
     */
    void rasterRamp_toSymbolLayerSetsCorrectKind();
    /**
     *
     * @brief Round-trip clamp bounds, NoData RGB and 0.6 opacity.
     * The NoData comparison uses rgb(), so alpha preservation is not asserted here.
     *
     */
    void rasterRamp_roundTripPreservesScalarFields();
    /**
     *
     * @brief Require viridis stop count and first-stop RGB to survive typed symbol conversion.
     * Other stop colors/positions are not individually compared.
     *
     */
    void rasterRamp_rampSubObjectRoundTrips();
    /**
     *
     * @brief Require Quantile method and seven-bin metadata to survive typed symbol conversion.
     *
     */
    void rasterRamp_binnerSubObjectRoundTrips();
    /**
     *
     * @brief Require out-of-range alpha 1.5/-0.2 to decode to one/zero.
     *
     */
    void rasterRamp_opacityClampsToUnit();
    /**
     *
     * @brief Require empty raster symbol properties to use default clamp bounds and opacity.
     *
     */
    void rasterRamp_partialPropsFallBackToDefaults();

    // HillshadeSymbolLayerSpec
    /**
     *
     * @brief Pin 315-degree azimuth/45-degree altitude, positive exaggeration and unit-interval
     * shadow floor.
     *
     */
    void hillshade_defaultsMatchShippedAU64Lite();
    /**
     *
     * @brief Require Hillshade symbol kind from the typed specification.
     *
     */
    void hillshade_toSymbolLayerSetsCorrectKind();
    /**
     *
     * @brief Round-trip hillshade azimuth/altitude degrees, exaggeration, shadow floor and Screen
     * blend token.
     *
     */
    void hillshade_roundTripPreservesEveryField();
    /**
     *
     * @brief Require decoded shadow floor 1.7 to clamp to one.
     *
     */
    void hillshade_shadowFloorClampsToUnit();
    /**
     *
     * @brief Require Overlay blend token to survive typed conversion without rendering a blend.
     *
     */
    void hillshade_blendModeStringPreserved();

    // ContourSymbolLayerSpec
    /**
     *
     * @brief Pin Lines contour default, positive logical-pixel width and zero label interval.
     *
     */
    void contour_defaultsAreLinesMode();
    /**
     *
     * @brief Require Contour symbol kind from its typed specification.
     *
     */
    void contour_toSymbolLayerSetsCorrectKind();
    /**
     *
     * @brief Round-trip contour mode, line RGB/width, label interval/format, bin method/count and
     * ramp stop count.
     * This does not compare every ramp stop or line alpha despite the broad case name.
     *
     */
    void contour_roundTripPreservesEveryField();
    /**
     *
     * @brief Require Lines, Filled and Both modes to survive typed symbol conversion.
     *
     */
    void contour_modesAllSerialise();
    /**
     *
     * @brief Require label interval ten and literal z-format text to survive typed conversion.
     *
     */
    void contour_labelEveryNRoundTrips();

    // MeshEdgeSymbolLayerSpec
    /**
     *
     * @brief Require valid default mesh-edge color, positive width and SolidLine pen.
     *
     */
    void meshEdge_defaultsAreSensible();
    /**
     *
     * @brief Require QPen to combine the specified color, fractional width and DashLine style.
     *
     */
    void meshEdge_toQPenComposesFields();
    /**
     *
     * @brief Require MeshEdge symbol kind from its typed specification.
     *
     */
    void meshEdge_toSymbolLayerSetsCorrectKind();
    /**
     *
     * @brief Round-trip mesh-edge RGB, width, dash and minimum zoom metadata.
     *
     */
    void meshEdge_roundTripPreservesEveryField();
    /**
     *
     * @brief Require minimum-zoom value twelve to survive typed conversion without evaluating
     * visibility.
     *
     */
    void meshEdge_lodMinZoomRoundTrips();

    // MeshNodeSymbolLayerSpec
    /**
     *
     * @brief Require mesh-node encoding to be readable as marker shape, size and outline width.
     * The assigned fill color is not separately asserted in this delegation case.
     *
     */
    void meshNode_delegatesToMarkerSpec();
    /**
     *
     * @brief Require MeshNode identity rather than SimpleMarker despite shared marker properties.
     *
     */
    void meshNode_toSymbolLayerSetsMeshNodeKindNotSimpleMarker();
    /**
     *
     * @brief Round-trip mesh-node star shape, logical-pixel size, fill RGB and outline width.
     * The assigned outline color is not separately compared.
     *
     */
    void meshNode_roundTripPreservesMarkerFields();
};

// ── Enum ─────────────────────────────────────────────────────────────

void TestRasterSymbolLayers::enum_newKindsRoundTripThroughString()
{
    const SymbolLayerKind newKinds[] = {SymbolLayerKind::RasterColorRamp,
                                        SymbolLayerKind::Hillshade, SymbolLayerKind::Contour,
                                        SymbolLayerKind::MeshEdge, SymbolLayerKind::MeshNode};
    for (SymbolLayerKind k : newKinds) {
        const QString s = symbolLayerKindToString(k);
        QVERIFY(!s.isEmpty());
        QCOMPARE(symbolLayerKindFromString(s), k);
    }
}

void TestRasterSymbolLayers::enum_contourModeRoundTrips()
{
    for (ContourMode m : {ContourMode::Lines, ContourMode::Filled, ContourMode::Both}) {
        const QString s = contourModeToString(m);
        QVERIFY(!s.isEmpty());
        QCOMPARE(contourModeFromString(s), m);
    }
    QCOMPARE(contourModeFromString(QStringLiteral("nope")), ContourMode::Lines);
}

// ── RasterColorRampSymbolLayerSpec ──────────────────────────────────

void TestRasterSymbolLayers::rasterRamp_defaultsAreSensible()
{
    RasterColorRampSymbolLayerSpec s;
    QVERIFY(s.opacity >= 0.0 && s.opacity <= 1.0);
    QVERIFY(s.clampMax >= s.clampMin);
}

void TestRasterSymbolLayers::rasterRamp_toSymbolLayerSetsCorrectKind()
{
    RasterColorRampSymbolLayerSpec s;
    QCOMPARE(s.toSymbolLayer().kind, SymbolLayerKind::RasterColorRamp);
}

void TestRasterSymbolLayers::rasterRamp_roundTripPreservesScalarFields()
{
    RasterColorRampSymbolLayerSpec s;
    s.clampMin = -2.5;
    s.clampMax = 50.0;
    s.noDataColor = QColor(100, 100, 100, 0);
    s.opacity = 0.6;

    SymbolLayer layer = s.toSymbolLayer();
    RasterColorRampSymbolLayerSpec back = RasterColorRampSymbolLayerSpec::fromSymbolLayer(layer);
    QCOMPARE(back.clampMin, s.clampMin);
    QCOMPARE(back.clampMax, s.clampMax);
    QCOMPARE(back.noDataColor.rgb(), s.noDataColor.rgb());
    QCOMPARE(back.opacity, s.opacity);
}

void TestRasterSymbolLayers::rasterRamp_rampSubObjectRoundTrips()
{
    RasterColorRampSymbolLayerSpec s;
    s.ramp = RasterColorRamp::viridis();
    SymbolLayer layer = s.toSymbolLayer();
    RasterColorRampSymbolLayerSpec back = RasterColorRampSymbolLayerSpec::fromSymbolLayer(layer);

    QCOMPARE(back.ramp.stops.size(), s.ramp.stops.size());
    if (!s.ramp.stops.isEmpty()) {
        QCOMPARE(back.ramp.stops.first().second.rgb(), s.ramp.stops.first().second.rgb());
    }
}

void TestRasterSymbolLayers::rasterRamp_binnerSubObjectRoundTrips()
{
    RasterColorRampSymbolLayerSpec s;
    s.binner.setMethod(BinMethod::Quantile);
    s.binner.setBinCount(7);

    SymbolLayer layer = s.toSymbolLayer();
    RasterColorRampSymbolLayerSpec back = RasterColorRampSymbolLayerSpec::fromSymbolLayer(layer);
    QCOMPARE(back.binner.method(), BinMethod::Quantile);
    QCOMPARE(back.binner.binCount(), 7);
}

void TestRasterSymbolLayers::rasterRamp_opacityClampsToUnit()
{
    RasterColorRampSymbolLayerSpec s;
    s.opacity = 1.5;
    SymbolLayer layer = s.toSymbolLayer();
    RasterColorRampSymbolLayerSpec back = RasterColorRampSymbolLayerSpec::fromSymbolLayer(layer);
    QCOMPARE(back.opacity, 1.0);

    s.opacity = -0.2;
    SymbolLayer layer2 = s.toSymbolLayer();
    back = RasterColorRampSymbolLayerSpec::fromSymbolLayer(layer2);
    QCOMPARE(back.opacity, 0.0);
}

void TestRasterSymbolLayers::rasterRamp_partialPropsFallBackToDefaults()
{
    SymbolLayer layer;
    layer.kind = SymbolLayerKind::RasterColorRamp;
    // Empty props.
    RasterColorRampSymbolLayerSpec back = RasterColorRampSymbolLayerSpec::fromSymbolLayer(layer);
    RasterColorRampSymbolLayerSpec defaults;
    QCOMPARE(back.clampMin, defaults.clampMin);
    QCOMPARE(back.clampMax, defaults.clampMax);
    QCOMPARE(back.opacity, defaults.opacity);
}

// ── HillshadeSymbolLayerSpec ────────────────────────────────────────

void TestRasterSymbolLayers::hillshade_defaultsMatchShippedAU64Lite()
{
    HillshadeSymbolLayerSpec s;
    // §N AU.6.4-lite shipped defaults: NW light (azimuth ≈ 315°) at 45° altitude.
    QCOMPARE(s.azimuthDeg, 315.0);
    QCOMPARE(s.altitudeDeg, 45.0);
    QVERIFY(s.zExaggeration > 0.0);
    QVERIFY(s.shadowFloor >= 0.0 && s.shadowFloor <= 1.0);
}

void TestRasterSymbolLayers::hillshade_toSymbolLayerSetsCorrectKind()
{
    HillshadeSymbolLayerSpec s;
    QCOMPARE(s.toSymbolLayer().kind, SymbolLayerKind::Hillshade);
}

void TestRasterSymbolLayers::hillshade_roundTripPreservesEveryField()
{
    HillshadeSymbolLayerSpec s;
    s.azimuthDeg = 270.0;
    s.altitudeDeg = 60.0;
    s.zExaggeration = 2.5;
    s.shadowFloor = 0.35;
    s.blendMode = QStringLiteral("Screen");

    SymbolLayer layer = s.toSymbolLayer();
    HillshadeSymbolLayerSpec back = HillshadeSymbolLayerSpec::fromSymbolLayer(layer);
    QCOMPARE(back.azimuthDeg, s.azimuthDeg);
    QCOMPARE(back.altitudeDeg, s.altitudeDeg);
    QCOMPARE(back.zExaggeration, s.zExaggeration);
    QCOMPARE(back.shadowFloor, s.shadowFloor);
    QCOMPARE(back.blendMode, s.blendMode);
}

void TestRasterSymbolLayers::hillshade_shadowFloorClampsToUnit()
{
    HillshadeSymbolLayerSpec s;
    s.shadowFloor = 1.7;
    SymbolLayer layer = s.toSymbolLayer();
    HillshadeSymbolLayerSpec back = HillshadeSymbolLayerSpec::fromSymbolLayer(layer);
    QCOMPARE(back.shadowFloor, 1.0);
}

void TestRasterSymbolLayers::hillshade_blendModeStringPreserved()
{
    HillshadeSymbolLayerSpec s;
    s.blendMode = QStringLiteral("Overlay");
    HillshadeSymbolLayerSpec back = HillshadeSymbolLayerSpec::fromSymbolLayer(s.toSymbolLayer());
    QCOMPARE(back.blendMode, QStringLiteral("Overlay"));
}

// ── ContourSymbolLayerSpec ───────────────────────────────────────────

void TestRasterSymbolLayers::contour_defaultsAreLinesMode()
{
    ContourSymbolLayerSpec s;
    QCOMPARE(s.mode, ContourMode::Lines);
    QVERIFY(s.lineWidthPx > 0.0);
    QCOMPARE(s.labelEveryN, 0);
}

void TestRasterSymbolLayers::contour_toSymbolLayerSetsCorrectKind()
{
    ContourSymbolLayerSpec s;
    QCOMPARE(s.toSymbolLayer().kind, SymbolLayerKind::Contour);
}

void TestRasterSymbolLayers::contour_roundTripPreservesEveryField()
{
    ContourSymbolLayerSpec s;
    s.mode = ContourMode::Both;
    s.lineColor = QColor(255, 100, 0);
    s.lineWidthPx = 1.5;
    s.labelEveryN = 5;
    s.labelFormat = QStringLiteral("%.2f m");
    s.binner.setMethod(BinMethod::EqualInterval);
    s.binner.setBinCount(8);
    s.ramp = RasterColorRamp::plasma();

    SymbolLayer layer = s.toSymbolLayer();
    ContourSymbolLayerSpec back = ContourSymbolLayerSpec::fromSymbolLayer(layer);
    QCOMPARE(back.mode, s.mode);
    QCOMPARE(back.lineColor.rgb(), s.lineColor.rgb());
    QCOMPARE(back.lineWidthPx, s.lineWidthPx);
    QCOMPARE(back.labelEveryN, s.labelEveryN);
    QCOMPARE(back.labelFormat, s.labelFormat);
    QCOMPARE(back.binner.method(), s.binner.method());
    QCOMPARE(back.binner.binCount(), s.binner.binCount());
    QCOMPARE(back.ramp.stops.size(), s.ramp.stops.size());
}

void TestRasterSymbolLayers::contour_modesAllSerialise()
{
    for (ContourMode m : {ContourMode::Lines, ContourMode::Filled, ContourMode::Both}) {
        ContourSymbolLayerSpec s;
        s.mode = m;
        SymbolLayer layer = s.toSymbolLayer();
        ContourSymbolLayerSpec back = ContourSymbolLayerSpec::fromSymbolLayer(layer);
        QCOMPARE(back.mode, m);
    }
}

void TestRasterSymbolLayers::contour_labelEveryNRoundTrips()
{
    ContourSymbolLayerSpec s;
    s.labelEveryN = 10;
    s.labelFormat = QStringLiteral("z=%.1f m");
    ContourSymbolLayerSpec back = ContourSymbolLayerSpec::fromSymbolLayer(s.toSymbolLayer());
    QCOMPARE(back.labelEveryN, 10);
    QCOMPARE(back.labelFormat, QStringLiteral("z=%.1f m"));
}

// ── MeshEdgeSymbolLayerSpec ─────────────────────────────────────────

void TestRasterSymbolLayers::meshEdge_defaultsAreSensible()
{
    MeshEdgeSymbolLayerSpec s;
    QVERIFY(s.color.isValid());
    QVERIFY(s.width > 0.0);
    QCOMPARE(s.penStyle, Qt::SolidLine);
}

void TestRasterSymbolLayers::meshEdge_toQPenComposesFields()
{
    MeshEdgeSymbolLayerSpec s;
    s.color = QColor(33, 44, 55);
    s.width = 1.25;
    s.penStyle = Qt::DashLine;
    QPen pen = s.toQPen();
    QCOMPARE(pen.color(), QColor(33, 44, 55));
    QCOMPARE(pen.widthF(), 1.25);
    QCOMPARE(pen.style(), Qt::DashLine);
}

void TestRasterSymbolLayers::meshEdge_toSymbolLayerSetsCorrectKind()
{
    MeshEdgeSymbolLayerSpec s;
    QCOMPARE(s.toSymbolLayer().kind, SymbolLayerKind::MeshEdge);
}

void TestRasterSymbolLayers::meshEdge_roundTripPreservesEveryField()
{
    MeshEdgeSymbolLayerSpec s;
    s.color = QColor(10, 20, 30);
    s.width = 0.75;
    s.penStyle = Qt::DashDotLine;
    s.lodMinZoom = 8;

    SymbolLayer layer = s.toSymbolLayer();
    MeshEdgeSymbolLayerSpec back = MeshEdgeSymbolLayerSpec::fromSymbolLayer(layer);
    QCOMPARE(back.color.rgb(), s.color.rgb());
    QCOMPARE(back.width, s.width);
    QCOMPARE(back.penStyle, s.penStyle);
    QCOMPARE(back.lodMinZoom, s.lodMinZoom);
}

void TestRasterSymbolLayers::meshEdge_lodMinZoomRoundTrips()
{
    MeshEdgeSymbolLayerSpec s;
    s.lodMinZoom = 12;
    MeshEdgeSymbolLayerSpec back = MeshEdgeSymbolLayerSpec::fromSymbolLayer(s.toSymbolLayer());
    QCOMPARE(back.lodMinZoom, 12);
}

// ── MeshNodeSymbolLayerSpec ─────────────────────────────────────────

void TestRasterSymbolLayers::meshNode_delegatesToMarkerSpec()
{
    MeshNodeSymbolLayerSpec s;
    s.marker.shape = MarkerShape::Diamond;
    s.marker.sizePx = 6.0;
    s.marker.fillColor = QColor(200, 50, 100);
    s.marker.outlineWidth = 1.0;

    SymbolLayer layer = s.toSymbolLayer();
    MarkerSymbolLayerSpec asMarker = MarkerSymbolLayerSpec::fromSymbolLayer(layer);
    QCOMPARE(asMarker.shape, MarkerShape::Diamond);
    QCOMPARE(asMarker.sizePx, 6.0);
    QCOMPARE(asMarker.outlineWidth, 1.0);
}

void TestRasterSymbolLayers::meshNode_toSymbolLayerSetsMeshNodeKindNotSimpleMarker()
{
    MeshNodeSymbolLayerSpec s;
    SymbolLayer layer = s.toSymbolLayer();
    QCOMPARE(layer.kind, SymbolLayerKind::MeshNode);
    // Not SimpleMarker — the paint host needs to distinguish them.
    QVERIFY(layer.kind != SymbolLayerKind::SimpleMarker);
}

void TestRasterSymbolLayers::meshNode_roundTripPreservesMarkerFields()
{
    MeshNodeSymbolLayerSpec s;
    s.marker.shape = MarkerShape::Star;
    s.marker.sizePx = 9.0;
    s.marker.fillColor = QColor(50, 200, 50);
    s.marker.outlineColor = QColor(0, 0, 0);
    s.marker.outlineWidth = 0.5;

    SymbolLayer layer = s.toSymbolLayer();
    MeshNodeSymbolLayerSpec back = MeshNodeSymbolLayerSpec::fromSymbolLayer(layer);
    QCOMPARE(back.marker.shape, s.marker.shape);
    QCOMPARE(back.marker.sizePx, s.marker.sizePx);
    QCOMPARE(back.marker.fillColor.rgb(), s.marker.fillColor.rgb());
    QCOMPARE(back.marker.outlineWidth, s.marker.outlineWidth);
}

QTEST_MAIN(TestRasterSymbolLayers)
#include "test_rastersymbollayers.moc"
