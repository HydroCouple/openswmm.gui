/*!
 * \file   test_unclassedcolorsrenderer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Tests for Slice Z.9 — UnclassedColorsRenderer.
 *
 *         Success criterion (RENDERING_RULE_MODEL_PLAN.md §16, Z.9):
 *           - Continuous color mapping over [minValue, maxValue].
 *           - Below-min / above-max / no-data branches all produce
 *             distinct sentinel colours.
 *           - symbolFor overrides the SymbolLayer "color" prop only when
 *             the slot exists (round-trip with GraduatedRenderer's
 *             convention).
 *           - JSON round-trip preserves every field.
 *           - Rule::fromJson dispatches "unclassed" to this renderer.
 *           - Legend emits the requested number of items, evenly spaced
 *             across [min, max].
 *           - Clone is independent of the original.
 */

#include <QColor>
#include <QSignalSpy>
#include <QtTest/QtTest>

#include <cmath>

#include "render/colorramp.h"
#include "render/renderers/unclassedcolorsrenderer.h"
#include "render/rule.h"
#include "render/symbollayer.h"
#include "render/symbolstyle.h"

using namespace OpenSWMM::Render;

class TestUnclassedColorsRenderer : public QObject
{
    Q_OBJECT
private slots:
    // colorForValue
    void colorForValue_inRangeMatchesRampSampling();
    void colorForValue_belowMinUsesRampStartByDefault();
    void colorForValue_belowMinUsesExplicitSentinelWhenSet();
    void colorForValue_aboveMaxUsesRampEndByDefault();
    void colorForValue_aboveMaxUsesExplicitSentinelWhenSet();
    void colorForValue_nanReturnsNoData();
    void colorForValue_degenerateRangeReturnsMidpoint();
    void colorForValue_clampsToZeroOne();

    // symbolFor
    void symbolFor_overridesColorPropWhenPresent();
    void symbolFor_leavesNonColorPropsUntouched();
    void symbolFor_missingAttributeReturnsNoData();
    void symbolFor_nonNumericAttributeReturnsNoData();

    // Legend
    void legend_emitsLegendLabelCountItems();
    void legend_firstItemAtMinLastItemAtMax();
    void legend_evenlySpacedValues();
    void legend_minimumTwoEvenWhenSetToOne();

    // JSON
    void json_roundTripPreservesEveryField();
    void json_omitsDefaultedSentinels();
    void json_rendererIdIsUnclassed();

    // Clone
    void clone_isIndependent();

    // Rule factory integration
    void ruleFactory_dispatchesUnclassedFromJson();
};

// ── colorForValue ────────────────────────────────────────────────────

void TestUnclassedColorsRenderer::colorForValue_inRangeMatchesRampSampling()
{
    UnclassedColorsRenderer r;
    r.setRange(0.0, 10.0);
    r.setRamp(RasterColorRamp::viridis(0.0, 10.0));

    const QColor midRendered = r.colorForValue(5.0);
    const QColor midDirect   = r.ramp().colorAt(0.5);
    QCOMPARE(midRendered.rgb(), midDirect.rgb());
}

void TestUnclassedColorsRenderer::colorForValue_belowMinUsesRampStartByDefault()
{
    UnclassedColorsRenderer r;
    r.setRange(0.0, 10.0);
    r.setRamp(RasterColorRamp::viridis(0.0, 10.0));
    QCOMPARE(r.colorForValue(-5.0).rgb(), r.ramp().colorAt(0.0).rgb());
}

void TestUnclassedColorsRenderer::colorForValue_belowMinUsesExplicitSentinelWhenSet()
{
    UnclassedColorsRenderer r;
    r.setRange(0.0, 10.0);
    r.setBelowRangeColor(QColor(Qt::red));
    QCOMPARE(r.colorForValue(-5.0), QColor(Qt::red));
}

void TestUnclassedColorsRenderer::colorForValue_aboveMaxUsesRampEndByDefault()
{
    UnclassedColorsRenderer r;
    r.setRange(0.0, 10.0);
    r.setRamp(RasterColorRamp::viridis(0.0, 10.0));
    QCOMPARE(r.colorForValue(100.0).rgb(), r.ramp().colorAt(1.0).rgb());
}

void TestUnclassedColorsRenderer::colorForValue_aboveMaxUsesExplicitSentinelWhenSet()
{
    UnclassedColorsRenderer r;
    r.setRange(0.0, 10.0);
    r.setAboveRangeColor(QColor(Qt::magenta));
    QCOMPARE(r.colorForValue(100.0), QColor(Qt::magenta));
}

void TestUnclassedColorsRenderer::colorForValue_nanReturnsNoData()
{
    UnclassedColorsRenderer r;
    r.setNoDataColor(QColor(Qt::gray));
    QCOMPARE(r.colorForValue(std::nan("")), QColor(Qt::gray));
    QCOMPARE(r.colorForValue(std::numeric_limits<double>::infinity()), QColor(Qt::gray));
}

void TestUnclassedColorsRenderer::colorForValue_degenerateRangeReturnsMidpoint()
{
    UnclassedColorsRenderer r;
    r.setRange(5.0, 5.0);  // min == max → degenerate
    r.setRamp(RasterColorRamp::viridis(5.0, 5.0));
    const QColor c = r.colorForValue(5.0);
    QVERIFY(c.isValid());
    QCOMPARE(c.rgb(), r.ramp().colorAt(0.5).rgb());
}

void TestUnclassedColorsRenderer::colorForValue_clampsToZeroOne()
{
    UnclassedColorsRenderer r;
    r.setRange(0.0, 10.0);
    r.setRamp(RasterColorRamp::viridis(0.0, 10.0));
    QCOMPARE(r.colorForValue(0.0).rgb(),  r.ramp().colorAt(0.0).rgb());
    QCOMPARE(r.colorForValue(10.0).rgb(), r.ramp().colorAt(1.0).rgb());
}

// ── symbolFor ───────────────────────────────────────────────────────

void TestUnclassedColorsRenderer::symbolFor_overridesColorPropWhenPresent()
{
    UnclassedColorsRenderer r;
    r.setClassifyAttribute(QStringLiteral("depth"));
    r.setRange(0.0, 10.0);
    SymbolStyle base;
    SymbolLayer layer;
    layer.kind = SymbolLayerKind::SimpleMarker;
    layer.props[QStringLiteral("color")] = QStringLiteral("#FF000000");
    layer.props[QStringLiteral("size")]  = 8.0;
    base.layers.append(layer);
    r.setBaseSymbol(base);

    QVariantMap attrs;
    attrs.insert(QStringLiteral("depth"), 5.0);
    SymbolStyle s = r.symbolFor({}, attrs);
    QVERIFY(s.layers.first().props.contains(QStringLiteral("color")));
    // Colour changed from the original black.
    QVERIFY(s.layers.first().props.value(QStringLiteral("color")).toString()
            != QStringLiteral("#FF000000"));
}

void TestUnclassedColorsRenderer::symbolFor_leavesNonColorPropsUntouched()
{
    UnclassedColorsRenderer r;
    r.setClassifyAttribute(QStringLiteral("v"));
    SymbolStyle base;
    SymbolLayer layer;
    layer.kind = SymbolLayerKind::SimpleMarker;
    layer.props[QStringLiteral("size")] = 12.5;
    base.layers.append(layer);
    r.setBaseSymbol(base);

    QVariantMap attrs;
    attrs.insert(QStringLiteral("v"), 0.5);
    SymbolStyle s = r.symbolFor({}, attrs);
    // No "color" key was present → renderer must not insert one.
    QVERIFY(!s.layers.first().props.contains(QStringLiteral("color")));
    QCOMPARE(s.layers.first().props.value(QStringLiteral("size")).toDouble(), 12.5);
}

void TestUnclassedColorsRenderer::symbolFor_missingAttributeReturnsNoData()
{
    UnclassedColorsRenderer r;
    r.setClassifyAttribute(QStringLiteral("missing"));
    r.setNoDataColor(QColor(Qt::yellow));
    SymbolStyle base;
    SymbolLayer layer;
    layer.props[QStringLiteral("color")] = QStringLiteral("#FF000000");
    base.layers.append(layer);
    r.setBaseSymbol(base);

    SymbolStyle s = r.symbolFor({}, QVariantMap{});
    QCOMPARE(QColor(s.layers.first().props.value(QStringLiteral("color")).toString()),
             QColor(Qt::yellow));
}

void TestUnclassedColorsRenderer::symbolFor_nonNumericAttributeReturnsNoData()
{
    UnclassedColorsRenderer r;
    r.setClassifyAttribute(QStringLiteral("kind"));
    r.setNoDataColor(QColor(Qt::cyan));
    SymbolStyle base;
    SymbolLayer layer;
    layer.props[QStringLiteral("color")] = QStringLiteral("#FF000000");
    base.layers.append(layer);
    r.setBaseSymbol(base);

    QVariantMap attrs;
    attrs.insert(QStringLiteral("kind"), QStringLiteral("apples"));
    SymbolStyle s = r.symbolFor({}, attrs);
    QCOMPARE(QColor(s.layers.first().props.value(QStringLiteral("color")).toString()),
             QColor(Qt::cyan));
}

// ── Legend ──────────────────────────────────────────────────────────

void TestUnclassedColorsRenderer::legend_emitsLegendLabelCountItems()
{
    UnclassedColorsRenderer r;
    r.setLegendLabelCount(7);
    QCOMPARE(r.legendSymbolItems().size(), 7);
}

void TestUnclassedColorsRenderer::legend_firstItemAtMinLastItemAtMax()
{
    UnclassedColorsRenderer r;
    r.setRange(0.0, 100.0);
    r.setLegendLabelCount(5);
    const auto items = r.legendSymbolItems();
    QCOMPARE(items.first().range.first, 0.0);
    QCOMPARE(items.last().range.first, 100.0);
}

void TestUnclassedColorsRenderer::legend_evenlySpacedValues()
{
    UnclassedColorsRenderer r;
    r.setRange(0.0, 10.0);
    r.setLegendLabelCount(3);
    const auto items = r.legendSymbolItems();
    QCOMPARE(items.size(), 3);
    QCOMPARE(items[0].range.first, 0.0);
    QCOMPARE(items[1].range.first, 5.0);
    QCOMPARE(items[2].range.first, 10.0);
}

void TestUnclassedColorsRenderer::legend_minimumTwoEvenWhenSetToOne()
{
    UnclassedColorsRenderer r;
    r.setLegendLabelCount(1);
    QCOMPARE(r.legendLabelCount(), 2);
}

// ── JSON ────────────────────────────────────────────────────────────

void TestUnclassedColorsRenderer::json_roundTripPreservesEveryField()
{
    UnclassedColorsRenderer r;
    r.setClassifyAttribute(QStringLiteral("flow"));
    r.setRange(-2.5, 50.0);
    r.setRamp(RasterColorRamp::plasma(-2.5, 50.0));
    r.setBelowRangeColor(QColor(11, 22, 33));
    r.setAboveRangeColor(QColor(200, 200, 200));
    r.setNoDataColor(QColor(64, 64, 64));
    r.setLegendLabelCount(8);
    SymbolStyle base;
    SymbolLayer layer;
    layer.props[QStringLiteral("color")] = QStringLiteral("#FF112233");
    base.layers.append(layer);
    r.setBaseSymbol(base);

    UnclassedColorsRenderer back;
    back.fromJson(r.toJson());
    QCOMPARE(back.classifyAttribute(), QStringLiteral("flow"));
    QCOMPARE(back.minValue(),          -2.5);
    QCOMPARE(back.maxValue(),          50.0);
    QCOMPARE(back.belowRangeColor().rgb(), QColor(11, 22, 33).rgb());
    QCOMPARE(back.aboveRangeColor().rgb(), QColor(200, 200, 200).rgb());
    QCOMPARE(back.noDataColor().rgb(), QColor(64, 64, 64).rgb());
    QCOMPARE(back.legendLabelCount(),  8);
}

void TestUnclassedColorsRenderer::json_omitsDefaultedSentinels()
{
    UnclassedColorsRenderer r;
    const QJsonObject j = r.toJson();
    // No explicit below/above/noData → keys absent.
    QVERIFY(!j.contains(QStringLiteral("belowRange")));
    QVERIFY(!j.contains(QStringLiteral("aboveRange")));
    QVERIFY(!j.contains(QStringLiteral("legendLabelCount"))); // default 5
}

void TestUnclassedColorsRenderer::json_rendererIdIsUnclassed()
{
    UnclassedColorsRenderer r;
    QCOMPARE(r.rendererId(), QStringLiteral("unclassed"));
    QCOMPARE(r.toJson().value(QStringLiteral("id")).toString(),
             QStringLiteral("unclassed"));
}

// ── Clone ───────────────────────────────────────────────────────────

void TestUnclassedColorsRenderer::clone_isIndependent()
{
    UnclassedColorsRenderer r;
    r.setClassifyAttribute(QStringLiteral("v"));
    r.setRange(0.0, 5.0);
    auto cloned = r.clone();
    // Mutate original; cloned must not change.
    r.setRange(100.0, 200.0);
    QCOMPARE(cloned->rendererId(), QStringLiteral("unclassed"));
    auto *casted = dynamic_cast<UnclassedColorsRenderer *>(cloned.get());
    QVERIFY(casted);
    QCOMPARE(casted->minValue(), 0.0);
    QCOMPARE(casted->maxValue(), 5.0);
}

// ── Rule factory integration ────────────────────────────────────────

void TestUnclassedColorsRenderer::ruleFactory_dispatchesUnclassedFromJson()
{
    UnclassedColorsRenderer src;
    src.setClassifyAttribute(QStringLiteral("depth"));
    src.setRange(0.0, 3.0);

    Rule rule(QStringLiteral("Unclassed depth"),
              std::unique_ptr<IFeatureRenderer>(src.clone().release()));
    auto back = Rule::fromJson(rule.toJson());
    QVERIFY(back);
    QVERIFY(back->renderer());
    QCOMPARE(back->renderer()->rendererId(), QStringLiteral("unclassed"));
    auto *casted = dynamic_cast<UnclassedColorsRenderer *>(back->renderer());
    QVERIFY(casted);
    QCOMPARE(casted->classifyAttribute(), QStringLiteral("depth"));
    QCOMPARE(casted->minValue(), 0.0);
    QCOMPARE(casted->maxValue(), 3.0);
}

QTEST_MAIN(TestUnclassedColorsRenderer)
#include "test_unclassedcolorsrenderer.moc"
