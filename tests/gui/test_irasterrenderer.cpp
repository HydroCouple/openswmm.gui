/**
 * @file      test_irasterrenderer.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Tests for Phase 8.13.6.7 — IRasterRenderer interface + the
 *         SingleBandPseudoColorRenderer stub.
 *
 *         The raster renderer hierarchy is the sister of IFeatureRenderer
 *         (covered by test_ifeaturerenderer.cpp).  Tests here focus on the
 *         narrower per-pixel contract:
 *           - colorForValue() interpolates linearly between adjacent stops
 *           - out-of-range values respect the clamp policy
 *           - NaN / no-data values render as Qt::transparent
 *           - legendSymbolItems() emits one item per stop
 *           - JSON round-trip preserves min/max/stops/clamp flags
 *           - clone() returns an independent copy
 *
 *         Self-contained: pulls in only rendering implementation files (no Qt widgets, no
 *         GDAL, no engine).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include <QJsonArray>
#include <QJsonObject>
#include <QtTest/QtTest>

#include <cmath>
#include <limits>
#include <memory>

#include "render/irasterrenderer.h"
#include "render/legendsymbolitem.h"
#include "render/renderers/singlebandpseudocolorrenderer.h"
#include "render/symbollayer.h"
#include "render/symbolstyle.h"

using namespace OpenSWMM::Render;

namespace
{

// Helper: build a two-stop black→white ramp on [0, 1].
/**
 *
 * @brief Build the two-stop grayscale fixture renderer.
 * @return Owned by-value renderer with scalar range [0,1], black/white stops at
 * normalized positions 0/1 and default remaining policies. No files/GPU/model access.
 *
 */
SingleBandPseudoColorRenderer makeBlackWhite()
{
    SingleBandPseudoColorRenderer r;
    r.setRange(0.0, 1.0);
    r.setStops({
        {0.0, QColor(Qt::black)},
        {1.0, QColor(Qt::white)},
    });
    return r;
}

} // namespace

class TestIRasterRenderer : public QObject
{
    Q_OBJECT

private slots:
    /**
     *
     * @brief Pin singlebandpseudocolor renderer identity token.
     *
     */
    void rendererId_isStable();
    /**
     *
     * @brief Require exact black/white colors at scalar endpoints 0/1.
     *
     */
    void colorForValue_endpoints();
    /**
     *
     * @brief Require grayscale midpoint near channel value 128 with one-unit rounding tolerance.
     *
     */
    void colorForValue_midpointInterpolates();
    /**
     *
     * @brief Require scalar midpoint 5 in [0,10] to hit the explicit green normalized-middle
     * stop.
     *
     */
    void colorForValue_threeStopsHitsMiddleStop();
    /**
     *
     * @brief Require explicit NoData flag to override a finite value with transparency.
     *
     */
    void colorForValue_isNoDataReturnsTransparent();
    /**
     *
     * @brief Require NaN and positive infinity to be transparent.
     *
     */
    void colorForValue_nonFiniteReturnsTransparent();
    /**
     *
     * @brief Require a renderer without stops to return transparency.
     *
     */
    void colorForValue_emptyStopsReturnsTransparent();
    /**
     *
     * @brief Require below-minimum transparency even with legacy clampMin enabled.
     *
     */
    void colorForValue_clampMinReturnsTransparent();
    /**
     *
     * @brief Require above-range saturation to white with legacy clampMax enabled.
     *
     */
    void colorForValue_legacyClampMaxStillSaturates();
    /**
     *
     * @brief Require below-range transparency under default policies.
     *
     */
    void colorForValue_belowMinimumIsTransparent();
    /**
     *
     * @brief Require far above-range value to saturate to the last stop.
     *
     */
    void colorForValue_unclampedAboveReturnsLastStop();
    /**
     *
     * @brief Require an equal-bound scalar range at its only value to choose the first stop.
     *
     */
    void colorForValue_degenerateRangeReturnsFirstStop();
    /**
     *
     * @brief Require a single magenta stop to color two in-range probes identically.
     *
     */
    void colorForValue_singleStopActsAsConstant();
    /**
     *
     * @brief Require one legend item per stop with ascending sort indices and the first
     * SimpleFill color matching red.
     *
     */
    void legendSymbolItems_oneItemPerStop();
    /**
     *
     * @brief Round-trip range bounds, legacy clamp flags and every ordered stop/color.
     *
     */
    void jsonRoundTrip_preservesAllFields();
    /**
     *
     * @brief Require HSV-long interpolation round-trip and RGB fallback when interpolation
     * metadata is absent.
     *
     */
    void jsonRoundTrip_preservesInterp();
    /**
     *
     * @brief Require short-arc HSV red/green midpoint to differ from RGB and have high red/green
     * channels.
     *
     */
    void colorForValue_hsvInterpDiffersFromRgb();
    /**
     *
     * @brief Require an owned correctly typed clone to preserve its original range after the
     * source changes.
     *
     */
    void cloneIsIndependent();
};

void TestIRasterRenderer::rendererId_isStable()
{
    SingleBandPseudoColorRenderer r;
    QCOMPARE(r.rendererId(), QStringLiteral("singlebandpseudocolor"));
}

void TestIRasterRenderer::colorForValue_endpoints()
{
    const auto r = makeBlackWhite();
    QCOMPARE(r.colorForValue(0.0), QColor(Qt::black));
    QCOMPARE(r.colorForValue(1.0), QColor(Qt::white));
}

void TestIRasterRenderer::colorForValue_midpointInterpolates()
{
    const auto r = makeBlackWhite();
    const QColor mid = r.colorForValue(0.5);
    // Black-to-white midpoint should be roughly 50% gray.  We allow ±1
    // tolerance for rounding.
    QVERIFY(std::abs(mid.red() - 128) <= 1);
    QVERIFY(std::abs(mid.green() - 128) <= 1);
    QVERIFY(std::abs(mid.blue() - 128) <= 1);
}

void TestIRasterRenderer::colorForValue_threeStopsHitsMiddleStop()
{
    SingleBandPseudoColorRenderer r;
    r.setRange(0.0, 10.0);
    r.setStops({
        {0.0, QColor(255, 0, 0)},
        {0.5, QColor(0, 255, 0)},
        {1.0, QColor(0, 0, 255)},
    });
    // Value 5.0 corresponds to t = 0.5 — should hit the middle stop exactly.
    QCOMPARE(r.colorForValue(5.0), QColor(0, 255, 0));
}

void TestIRasterRenderer::colorForValue_isNoDataReturnsTransparent()
{
    const auto r = makeBlackWhite();
    QCOMPARE(r.colorForValue(0.5, /*isNoData=*/true), QColor(Qt::transparent));
}

void TestIRasterRenderer::colorForValue_nonFiniteReturnsTransparent()
{
    const auto r = makeBlackWhite();
    QCOMPARE(r.colorForValue(std::numeric_limits<double>::quiet_NaN()), QColor(Qt::transparent));
    QCOMPARE(r.colorForValue(std::numeric_limits<double>::infinity()), QColor(Qt::transparent));
}

void TestIRasterRenderer::colorForValue_emptyStopsReturnsTransparent()
{
    SingleBandPseudoColorRenderer r;
    r.setRange(0.0, 1.0);
    // No stops set.
    QCOMPARE(r.colorForValue(0.5), QColor(Qt::transparent));
}

void TestIRasterRenderer::colorForValue_clampMinReturnsTransparent()
{
    auto r = makeBlackWhite();
    r.setClampMin(true);
    QCOMPARE(r.colorForValue(-0.5), QColor(Qt::transparent));
}

void TestIRasterRenderer::colorForValue_legacyClampMaxStillSaturates()
{
    auto r = makeBlackWhite();
    r.setClampMax(true);
    QCOMPARE(r.colorForValue(2.0), QColor(Qt::white));
}

void TestIRasterRenderer::colorForValue_belowMinimumIsTransparent()
{
    const auto r = makeBlackWhite();
    QCOMPARE(r.colorForValue(-100.0), QColor(Qt::transparent));
}

void TestIRasterRenderer::colorForValue_unclampedAboveReturnsLastStop()
{
    const auto r = makeBlackWhite();
    QCOMPARE(r.colorForValue(100.0), QColor(Qt::white));
}

void TestIRasterRenderer::colorForValue_degenerateRangeReturnsFirstStop()
{
    SingleBandPseudoColorRenderer r;
    r.setRange(5.0, 5.0); // min == max
    r.setStops({
        {0.0, QColor(Qt::red)},
        {1.0, QColor(Qt::blue)},
    });
    // Any in-range value (== 5.0) collapses to the first stop's colour.
    QCOMPARE(r.colorForValue(5.0), QColor(Qt::red));
}

void TestIRasterRenderer::colorForValue_singleStopActsAsConstant()
{
    SingleBandPseudoColorRenderer r;
    r.setRange(0.0, 1.0);
    r.setStops({{0.5, QColor(Qt::magenta)}});
    QCOMPARE(r.colorForValue(0.25), QColor(Qt::magenta));
    QCOMPARE(r.colorForValue(0.75), QColor(Qt::magenta));
}

void TestIRasterRenderer::legendSymbolItems_oneItemPerStop()
{
    SingleBandPseudoColorRenderer r;
    r.setRange(0.0, 10.0);
    r.setStops({
        {0.0, QColor(Qt::red)},
        {0.5, QColor(Qt::green)},
        {1.0, QColor(Qt::blue)},
    });

    const auto items = r.legendSymbolItems();
    QCOMPARE(items.size(), 3);

    // sortIndex preserves ramp order.
    QCOMPARE(items.at(0).sortIndex, 0);
    QCOMPARE(items.at(1).sortIndex, 1);
    QCOMPARE(items.at(2).sortIndex, 2);

    // Each swatch is a single-layer SimpleFill whose colour matches the stop.
    // (Gap A1.2 — compare as QColor via the tolerant read; the canonical
    // in-memory encoding is a QColor variant, not a hex string.)
    const auto first = items.first().symbol.layers;
    QCOMPARE(first.size(), 1);
    QCOMPARE(first.first().kind, SymbolLayerKind::SimpleFill);
    QCOMPARE(SymbolProps::readColor(first.first().props, QStringLiteral("color")), QColor(Qt::red));
}

void TestIRasterRenderer::jsonRoundTrip_preservesAllFields()
{
    SingleBandPseudoColorRenderer in;
    in.setRange(2.5, 7.5);
    in.setStops({
        {0.0, QColor(Qt::darkRed)},
        {0.5, QColor(Qt::yellow)},
        {1.0, QColor(Qt::darkBlue)},
    });
    in.setClampMin(true);
    in.setClampMax(true);

    SingleBandPseudoColorRenderer out;
    out.fromJson(in.toJson());

    QCOMPARE(out.rendererId(), in.rendererId());
    QCOMPARE(out.minValue(), in.minValue());
    QCOMPARE(out.maxValue(), in.maxValue());
    QCOMPARE(out.clampMin(), in.clampMin());
    QCOMPARE(out.clampMax(), in.clampMax());
    QCOMPARE(out.stops().size(), in.stops().size());
    for (int i = 0; i < in.stops().size(); ++i) {
        QCOMPARE(out.stops().at(i).first, in.stops().at(i).first);
        // QColor::name(QColor::HexArgb) emits lowercase hex — comparing
        // QColor values directly side-steps that.
        QCOMPARE(out.stops().at(i).second, in.stops().at(i).second);
    }
}

void TestIRasterRenderer::jsonRoundTrip_preservesInterp()
{
    // P5/R-1 — interp must survive the JSON round-trip (default Rgb when
    // the key is absent, e.g. older .oswp files).
    SingleBandPseudoColorRenderer in = makeBlackWhite();
    in.setInterp(RampInterp::HsvLong);

    SingleBandPseudoColorRenderer out;
    out.fromJson(in.toJson());
    QCOMPARE(out.interp(), RampInterp::HsvLong);

    // Absent key defaults to Rgb.
    SingleBandPseudoColorRenderer legacy;
    QJsonObject j = in.toJson();
    j.remove(QStringLiteral("interp"));
    legacy.fromJson(j);
    QCOMPARE(legacy.interp(), RampInterp::Rgb);
}

void TestIRasterRenderer::colorForValue_hsvInterpDiffersFromRgb()
{
    // P5/R-1 — the renderer mirrors RasterColorRamp's interpolation so HSV
    // ramps don't silently degrade to RGB lerp. A red→green ramp midpoint
    // is a dull olive under RGB but a vivid yellow under HSV-short.
    SingleBandPseudoColorRenderer rgb;
    rgb.setRange(0.0, 1.0);
    rgb.setStops({{0.0, QColor(Qt::red)}, {1.0, QColor(Qt::green)}});
    rgb.setInterp(RampInterp::Rgb);

    SingleBandPseudoColorRenderer hsv = rgb;
    hsv.setInterp(RampInterp::HsvShort);

    const QColor midRgb = rgb.colorForValue(0.5);
    const QColor midHsv = hsv.colorForValue(0.5);
    QVERIFY(midRgb != midHsv);
    // HSV-short red→green passes through yellow: high red AND green.
    QVERIFY(midHsv.red() > 200);
    QVERIFY(midHsv.green() > 200);
}

void TestIRasterRenderer::cloneIsIndependent()
{
    auto original = makeBlackWhite();
    std::unique_ptr<IRasterRenderer> copy = original.clone();
    QVERIFY(copy != nullptr);
    QCOMPARE(copy->rendererId(), original.rendererId());

    // Mutate the original; the clone must not reflect the change.
    original.setRange(100.0, 200.0);

    auto *typed = dynamic_cast<SingleBandPseudoColorRenderer *>(copy.get());
    QVERIFY(typed != nullptr);
    QCOMPARE(typed->minValue(), 0.0);
    QCOMPARE(typed->maxValue(), 1.0);
}

QTEST_MAIN(TestIRasterRenderer)
#include "test_irasterrenderer.moc"
