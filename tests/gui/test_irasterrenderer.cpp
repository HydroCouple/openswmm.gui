/*!
 * \file   test_irasterrenderer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Tests for Phase 8.13.6.7 — IRasterRenderer interface + the
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
 *         Self-contained: pulls in only render/*.cpp (no Qt widgets, no
 *         GDAL, no engine).
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
SingleBandPseudoColorRenderer makeBlackWhite()
{
    SingleBandPseudoColorRenderer r;
    r.setRange(0.0, 1.0);
    r.setStops({
        { 0.0, QColor(Qt::black) },
        { 1.0, QColor(Qt::white) },
    });
    return r;
}

} // namespace

class TestIRasterRenderer : public QObject
{
    Q_OBJECT

private slots:
    void rendererId_isStable();
    void colorForValue_endpoints();
    void colorForValue_midpointInterpolates();
    void colorForValue_threeStopsHitsMiddleStop();
    void colorForValue_isNoDataReturnsTransparent();
    void colorForValue_nonFiniteReturnsTransparent();
    void colorForValue_emptyStopsReturnsTransparent();
    void colorForValue_clampMinReturnsTransparent();
    void colorForValue_clampMaxReturnsTransparent();
    void colorForValue_unclampedBelowReturnsFirstStop();
    void colorForValue_unclampedAboveReturnsLastStop();
    void colorForValue_degenerateRangeReturnsFirstStop();
    void colorForValue_singleStopActsAsConstant();
    void legendSymbolItems_oneItemPerStop();
    void jsonRoundTrip_preservesAllFields();
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
    QVERIFY(std::abs(mid.red()   - 128) <= 1);
    QVERIFY(std::abs(mid.green() - 128) <= 1);
    QVERIFY(std::abs(mid.blue()  - 128) <= 1);
}

void TestIRasterRenderer::colorForValue_threeStopsHitsMiddleStop()
{
    SingleBandPseudoColorRenderer r;
    r.setRange(0.0, 10.0);
    r.setStops({
        { 0.0, QColor(255,   0,   0) },
        { 0.5, QColor(  0, 255,   0) },
        { 1.0, QColor(  0,   0, 255) },
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
    QCOMPARE(r.colorForValue(std::numeric_limits<double>::quiet_NaN()),
             QColor(Qt::transparent));
    QCOMPARE(r.colorForValue(std::numeric_limits<double>::infinity()),
             QColor(Qt::transparent));
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

void TestIRasterRenderer::colorForValue_clampMaxReturnsTransparent()
{
    auto r = makeBlackWhite();
    r.setClampMax(true);
    QCOMPARE(r.colorForValue(2.0), QColor(Qt::transparent));
}

void TestIRasterRenderer::colorForValue_unclampedBelowReturnsFirstStop()
{
    const auto r = makeBlackWhite();
    QCOMPARE(r.colorForValue(-100.0), QColor(Qt::black));
}

void TestIRasterRenderer::colorForValue_unclampedAboveReturnsLastStop()
{
    const auto r = makeBlackWhite();
    QCOMPARE(r.colorForValue(100.0), QColor(Qt::white));
}

void TestIRasterRenderer::colorForValue_degenerateRangeReturnsFirstStop()
{
    SingleBandPseudoColorRenderer r;
    r.setRange(5.0, 5.0);   // min == max
    r.setStops({
        { 0.0, QColor(Qt::red)   },
        { 1.0, QColor(Qt::blue)  },
    });
    // Any in-range value (== 5.0) collapses to the first stop's colour.
    QCOMPARE(r.colorForValue(5.0), QColor(Qt::red));
}

void TestIRasterRenderer::colorForValue_singleStopActsAsConstant()
{
    SingleBandPseudoColorRenderer r;
    r.setRange(0.0, 1.0);
    r.setStops({ { 0.5, QColor(Qt::magenta) } });
    QCOMPARE(r.colorForValue(0.25), QColor(Qt::magenta));
    QCOMPARE(r.colorForValue(0.75), QColor(Qt::magenta));
}

void TestIRasterRenderer::legendSymbolItems_oneItemPerStop()
{
    SingleBandPseudoColorRenderer r;
    r.setRange(0.0, 10.0);
    r.setStops({
        { 0.0, QColor(Qt::red)   },
        { 0.5, QColor(Qt::green) },
        { 1.0, QColor(Qt::blue)  },
    });

    const auto items = r.legendSymbolItems();
    QCOMPARE(items.size(), 3);

    // sortIndex preserves ramp order.
    QCOMPARE(items.at(0).sortIndex, 0);
    QCOMPARE(items.at(1).sortIndex, 1);
    QCOMPARE(items.at(2).sortIndex, 2);

    // Each swatch is a single-layer SimpleFill whose colour matches the stop.
    const auto first = items.first().symbol.layers;
    QCOMPARE(first.size(), 1);
    QCOMPARE(first.first().kind, SymbolLayerKind::SimpleFill);
    QCOMPARE(first.first().props.value(QStringLiteral("color")).toString(),
             QColor(Qt::red).name(QColor::HexArgb));
}

void TestIRasterRenderer::jsonRoundTrip_preservesAllFields()
{
    SingleBandPseudoColorRenderer in;
    in.setRange(2.5, 7.5);
    in.setStops({
        { 0.0, QColor(Qt::darkRed)   },
        { 0.5, QColor(Qt::yellow)    },
        { 1.0, QColor(Qt::darkBlue)  },
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
    for (int i = 0; i < in.stops().size(); ++i)
    {
        QCOMPARE(out.stops().at(i).first,  in.stops().at(i).first);
        // QColor::name(QColor::HexArgb) emits lowercase hex — comparing
        // QColor values directly side-steps that.
        QCOMPARE(out.stops().at(i).second, in.stops().at(i).second);
    }
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
