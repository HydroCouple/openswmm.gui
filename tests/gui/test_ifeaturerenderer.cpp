/*!
 * \file   test_ifeaturerenderer.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Tests for Phases 8.13.6.1 + 8.13.6.2 — interfaces + concrete renderers.
 *
 *         8.13.6.1 (the first half of this file) covers the foundational
 *         types: FeatureRef, SymbolLayer, SymbolStyle, LegendSymbolItem,
 *         and the abstract IFeatureRenderer interface.
 *
 *         8.13.6.2 (the second half) adds the four concrete renderers:
 *           - SingleSymbolRenderer  (functional)
 *           - GraduatedRenderer     (functional)
 *           - CategorizedRenderer   (stub — JSON + paint, no UI)
 *           - RuleBasedRenderer     (stub — JSON + fallback paint)
 *
 *         The 8.13.6.2 cases also cover the unifying expectations the §J
 *         architecture lays down: legend-from-renderer correctness, clone
 *         independence, and JSON round-trip per renderer kind.
 */

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest/QtTest>

#include <cmath>

#include "render/featureref.h"
#include "render/ifeaturerenderer.h"
#include "render/legendsymbolitem.h"
#include "render/symbollayer.h"
#include "render/symbolstyle.h"

#include "render/renderers/categorizedrenderer.h"
#include "render/renderers/graduatedrenderer.h"
#include "render/renderers/rulebasedrenderer.h"
#include "render/renderers/singlesymbolrenderer.h"

using namespace OpenSWMM::Render;

class TestIFeatureRenderer : public QObject
{
    Q_OBJECT

private slots:
    void featureRef_defaults();
    void symbolLayerKind_roundTripTotal();
    void symbolLayerKind_unknownStringFallsBack();
    void symbolLayer_jsonRoundTrip();
    void symbolLayer_dataDefinedOverridesRoundTrip();
    void symbolStyle_jsonRoundTripEmpty();
    void symbolStyle_jsonRoundTripStacked();
    void symbolStyle_opacityDefault();
    void legendSymbolItem_jsonRoundTripGraduated();
    void legendSymbolItem_jsonRoundTripCategorical();
    void legendSymbolItem_effectiveLabelOverride();
    void legendSymbolItem_optionalFieldsOmitted();
    void interfaceCompiles_canDeclareDerivedClass();

    // Phase 8.13.6.2 — concrete renderers.
    void singleSymbolRenderer_paintAndLegend();
    void singleSymbolRenderer_jsonRoundTrip();
    void singleSymbolRenderer_cloneIsIndependent();
    void graduatedRenderer_binPickingClamps();
    void graduatedRenderer_legendItemsMatchBins();
    void graduatedRenderer_autoClassifyIgnoresNonFinite();
    void graduatedRenderer_autoClassifyConstantSampleWidens();
    void graduatedRenderer_symbolForOverridesColorInBaseSymbol();
    void graduatedRenderer_jsonRoundTrip();
    void categorizedRenderer_paintAndFallback();
    void categorizedRenderer_jsonRoundTrip();
    void ruleBasedRenderer_emptyExpressionMatches();
    void ruleBasedRenderer_jsonRoundTripWithScaleRange();
};

void TestIFeatureRenderer::featureRef_defaults()
{
    FeatureRef f;
    QCOMPARE(f.layerId, QString());
    QCOMPARE(f.featureIndex, -1);
    QCOMPARE(f.categoryHint, QString());
}

void TestIFeatureRenderer::symbolLayerKind_roundTripTotal()
{
    // Every enum value must round-trip cleanly through string.
    const QList<SymbolLayerKind> all = {
        SymbolLayerKind::SimpleMarker,
        SymbolLayerKind::SimpleLine,
        SymbolLayerKind::SimpleFill,
        SymbolLayerKind::MarkerLine,
        SymbolLayerKind::HatchFill,
        SymbolLayerKind::PatternFill,
        SymbolLayerKind::SvgMarker,
        SymbolLayerKind::FontMarker
    };
    for (SymbolLayerKind k : all)
    {
        const QString s = symbolLayerKindToString(k);
        QVERIFY2(!s.isEmpty(), "every kind must serialise to a non-empty token");
        QCOMPARE(symbolLayerKindFromString(s), k);
    }
}

void TestIFeatureRenderer::symbolLayerKind_unknownStringFallsBack()
{
    // Forward-compat: an unknown token must not throw or crash.
    QCOMPARE(symbolLayerKindFromString(QStringLiteral("futureKind")),
             SymbolLayerKind::SimpleMarker);
    QCOMPARE(symbolLayerKindFromString(QString()),
             SymbolLayerKind::SimpleMarker);
}

void TestIFeatureRenderer::symbolLayer_jsonRoundTrip()
{
    SymbolLayer in;
    in.kind = SymbolLayerKind::SimpleLine;
    in.props.insert(QStringLiteral("color"), QStringLiteral("#1f77b4"));
    in.props.insert(QStringLiteral("width"), 2.5);

    const QJsonObject j = in.toJson();

    SymbolLayer out;
    out.fromJson(j);

    QCOMPARE(out.kind, SymbolLayerKind::SimpleLine);
    QCOMPARE(out.props.value(QStringLiteral("color")).toString(), QStringLiteral("#1f77b4"));
    QCOMPARE(out.props.value(QStringLiteral("width")).toDouble(), 2.5);
}

void TestIFeatureRenderer::symbolLayer_dataDefinedOverridesRoundTrip()
{
    SymbolLayer in;
    in.kind = SymbolLayerKind::SimpleMarker;
    in.dataDefinedOverrides.insert(QStringLiteral("size"), QStringLiteral("$flow * 2"));
    in.dataDefinedOverrides.insert(QStringLiteral("color"),
                                   QStringLiteral("if($depth > 5, 'red', 'blue')"));

    SymbolLayer out;
    out.fromJson(in.toJson());

    QCOMPARE(out.dataDefinedOverrides.size(), 2);
    QCOMPARE(out.dataDefinedOverrides.value(QStringLiteral("size")),
             QStringLiteral("$flow * 2"));
    QCOMPARE(out.dataDefinedOverrides.value(QStringLiteral("color")),
             QStringLiteral("if($depth > 5, 'red', 'blue')"));
}

void TestIFeatureRenderer::symbolStyle_jsonRoundTripEmpty()
{
    SymbolStyle in;
    QCOMPARE(in.layers.size(), 0);
    QCOMPARE(in.opacity, 1.0);

    SymbolStyle out;
    out.fromJson(in.toJson());
    QCOMPARE(out.layers.size(), 0);
    QCOMPARE(out.opacity, 1.0);
}

void TestIFeatureRenderer::symbolStyle_jsonRoundTripStacked()
{
    // "Double-ringed manhole" style — outer ring + inner ring + centre cross.
    SymbolStyle in;
    in.opacity = 0.85;

    SymbolLayer outer;
    outer.kind = SymbolLayerKind::SimpleMarker;
    outer.props.insert(QStringLiteral("shape"), QStringLiteral("circle"));
    outer.props.insert(QStringLiteral("size"), 12.0);
    outer.props.insert(QStringLiteral("color"), QStringLiteral("#000000"));
    in.layers.append(outer);

    SymbolLayer inner;
    inner.kind = SymbolLayerKind::SimpleMarker;
    inner.props.insert(QStringLiteral("shape"), QStringLiteral("circle"));
    inner.props.insert(QStringLiteral("size"), 8.0);
    inner.props.insert(QStringLiteral("color"), QStringLiteral("#17becf"));
    in.layers.append(inner);

    SymbolLayer cross;
    cross.kind = SymbolLayerKind::SimpleMarker;
    cross.props.insert(QStringLiteral("shape"), QStringLiteral("cross"));
    cross.props.insert(QStringLiteral("size"), 4.0);
    in.layers.append(cross);

    SymbolStyle out;
    out.fromJson(in.toJson());

    QCOMPARE(out.layers.size(), 3);
    QCOMPARE(out.opacity, 0.85);
    QCOMPARE(out.layers.at(0).props.value(QStringLiteral("shape")).toString(),
             QStringLiteral("circle"));
    QCOMPARE(out.layers.at(0).props.value(QStringLiteral("size")).toDouble(), 12.0);
    QCOMPARE(out.layers.at(1).props.value(QStringLiteral("color")).toString(),
             QStringLiteral("#17becf"));
    QCOMPARE(out.layers.at(2).props.value(QStringLiteral("shape")).toString(),
             QStringLiteral("cross"));
}

void TestIFeatureRenderer::symbolStyle_opacityDefault()
{
    // Missing opacity in JSON must default to 1.0, not 0.0.
    QJsonObject j;
    j.insert(QStringLiteral("layers"), QJsonArray());
    SymbolStyle out;
    out.fromJson(j);
    QCOMPARE(out.opacity, 1.0);
}

void TestIFeatureRenderer::legendSymbolItem_jsonRoundTripGraduated()
{
    LegendSymbolItem in;
    in.label = QStringLiteral("0 – 0.5 ft³/s");
    in.range = { 0.0, 0.5 };
    in.userLabel = QStringLiteral("Low");
    in.visible = true;
    in.sortIndex = 0;

    SymbolLayer fill;
    fill.kind = SymbolLayerKind::SimpleFill;
    fill.props.insert(QStringLiteral("color"), QStringLiteral("#440154"));
    in.symbol.layers.append(fill);

    LegendSymbolItem out;
    out.fromJson(in.toJson());

    QCOMPARE(out.label, in.label);
    QCOMPARE(out.range.first, 0.0);
    QCOMPARE(out.range.second, 0.5);
    QCOMPARE(out.userLabel, QStringLiteral("Low"));
    QCOMPARE(out.visible, true);
    QCOMPARE(out.symbol.layers.size(), 1);
    QCOMPARE(out.symbol.layers.at(0).props.value(QStringLiteral("color")).toString(),
             QStringLiteral("#440154"));
}

void TestIFeatureRenderer::legendSymbolItem_jsonRoundTripCategorical()
{
    // Categorical items leave `range` as the (NaN, NaN) sentinel.
    LegendSymbolItem in;
    in.label = QStringLiteral("Storage");

    LegendSymbolItem out;
    out.fromJson(in.toJson());

    QCOMPARE(out.label, QStringLiteral("Storage"));
    QVERIFY(std::isnan(out.range.first));
    QVERIFY(std::isnan(out.range.second));
}

void TestIFeatureRenderer::legendSymbolItem_effectiveLabelOverride()
{
    LegendSymbolItem item;
    item.label = QStringLiteral("0 – 0.5");
    QCOMPARE(item.effectiveLabel(), QStringLiteral("0 – 0.5"));

    item.userLabel = QStringLiteral("Low");
    QCOMPARE(item.effectiveLabel(), QStringLiteral("Low"));

    // Empty userLabel falls back to label, not to empty string.
    item.userLabel.clear();
    QCOMPARE(item.effectiveLabel(), QStringLiteral("0 – 0.5"));
}

void TestIFeatureRenderer::legendSymbolItem_optionalFieldsOmitted()
{
    // Default-constructed item should produce compact JSON — no userLabel,
    // no explicit visible:true, no sortIndex:0, no range with NaN sentinels.
    LegendSymbolItem item;
    item.label = QStringLiteral("X");
    const QJsonObject j = item.toJson();
    QVERIFY(!j.contains(QStringLiteral("userLabel")));
    QVERIFY(!j.contains(QStringLiteral("visible")));
    QVERIFY(!j.contains(QStringLiteral("sortIndex")));
    QVERIFY(!j.contains(QStringLiteral("range")));
}

// A trivial concrete subclass to confirm IFeatureRenderer compiles as an
// abstract interface and can be derived from. No behaviour beyond returning
// stable defaults — full renderers arrive in sub-phase 8.13.6.2.
namespace
{
class StubRenderer final : public IFeatureRenderer
{
public:
    QString rendererId() const override { return QStringLiteral("stub"); }
    SymbolStyle symbolFor(const FeatureRef &, const QVariantMap &) const override
    {
        return SymbolStyle{};
    }
    QList<LegendSymbolItem> legendSymbolItems() const override { return {}; }
    QJsonObject toJson() const override
    {
        QJsonObject j;
        j.insert(QStringLiteral("id"), rendererId());
        return j;
    }
    void fromJson(const QJsonObject &) override {}
    std::unique_ptr<IFeatureRenderer> clone() const override
    {
        return std::make_unique<StubRenderer>(*this);
    }
};
} // namespace

void TestIFeatureRenderer::interfaceCompiles_canDeclareDerivedClass()
{
    std::unique_ptr<IFeatureRenderer> r = std::make_unique<StubRenderer>();
    QCOMPARE(r->rendererId(), QStringLiteral("stub"));
    QVERIFY(r->legendSymbolItems().isEmpty());

    // clone() returns an independent copy.
    auto r2 = r->clone();
    QVERIFY(r2 != nullptr);
    QCOMPARE(r2->rendererId(), QStringLiteral("stub"));

    // Round-trip the (empty) JSON shell.
    const QJsonObject j = r->toJson();
    QCOMPARE(j.value(QStringLiteral("id")).toString(), QStringLiteral("stub"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase 8.13.6.2 — concrete renderer test bodies.
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
// Build a SimpleMarker SymbolStyle with a "color" prop in hex format so the
// GraduatedRenderer's overrideColorInPlace logic has something to override.
SymbolStyle makeMarker(const QString &hexColor, double size = 8.0)
{
    SymbolLayer sl;
    sl.kind = SymbolLayerKind::SimpleMarker;
    sl.props.insert(QStringLiteral("shape"), QStringLiteral("circle"));
    sl.props.insert(QStringLiteral("size"),  size);
    sl.props.insert(QStringLiteral("color"), hexColor);
    SymbolStyle s;
    s.layers.append(sl);
    return s;
}
} // namespace

void TestIFeatureRenderer::singleSymbolRenderer_paintAndLegend()
{
    SingleSymbolRenderer r{ makeMarker(QStringLiteral("#1f77b4")),
                            QStringLiteral("Junctions") };

    // symbolFor returns the same style for every feature.
    FeatureRef f1{ QStringLiteral("L1"), 0, {} };
    FeatureRef f2{ QStringLiteral("L1"), 42, {} };
    SymbolStyle s1 = r.symbolFor(f1, {});
    SymbolStyle s2 = r.symbolFor(f2, {});
    QCOMPARE(s1.layers.size(), 1);
    QCOMPARE(s1.layers.at(0).props.value(QStringLiteral("color")).toString(),
             QStringLiteral("#1f77b4"));
    QCOMPARE(s2.layers.at(0).props.value(QStringLiteral("color")).toString(),
             QStringLiteral("#1f77b4"));

    // legend has exactly one item, with the user's label.
    const QList<LegendSymbolItem> items = r.legendSymbolItems();
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.first().label, QStringLiteral("Junctions"));
}

void TestIFeatureRenderer::singleSymbolRenderer_jsonRoundTrip()
{
    SingleSymbolRenderer in{ makeMarker(QStringLiteral("#2ca02c")), QStringLiteral("X") };
    SingleSymbolRenderer out;
    out.fromJson(in.toJson());

    QCOMPARE(out.rendererId(), QStringLiteral("single"));
    QCOMPARE(out.legendLabel(), QStringLiteral("X"));
    QCOMPARE(out.symbol().layers.size(), 1);
    QCOMPARE(out.symbol().layers.at(0).props.value(QStringLiteral("color")).toString(),
             QStringLiteral("#2ca02c"));
}

void TestIFeatureRenderer::singleSymbolRenderer_cloneIsIndependent()
{
    SingleSymbolRenderer src{ makeMarker(QStringLiteral("#ff7f0e")),
                              QStringLiteral("Original") };
    std::unique_ptr<IFeatureRenderer> copy = src.clone();
    QCOMPARE(copy->rendererId(), QStringLiteral("single"));

    // Mutating the source must not affect the clone.
    src.setLegendLabel(QStringLiteral("MUTATED"));
    auto *casted = dynamic_cast<SingleSymbolRenderer *>(copy.get());
    QVERIFY(casted != nullptr);
    QCOMPARE(casted->legendLabel(), QStringLiteral("Original"));
}

void TestIFeatureRenderer::graduatedRenderer_binPickingClamps()
{
    GraduatedRenderer r;
    r.setRange(0.0, 10.0);
    r.setBinColors({
        QColor(QStringLiteral("#440154")),  // bin 0: [0, 5)
        QColor(QStringLiteral("#fde725"))   // bin 1: [5, 10]
    });

    // Below range → bin 0.
    QCOMPARE(r.colorForValue(-5.0), QColor(QStringLiteral("#440154")));
    // In bin 0.
    QCOMPARE(r.colorForValue(2.5),  QColor(QStringLiteral("#440154")));
    // In bin 1.
    QCOMPARE(r.colorForValue(7.5),  QColor(QStringLiteral("#fde725")));
    // At exact max → still inside bin 1 (clamped).
    QCOMPARE(r.colorForValue(10.0), QColor(QStringLiteral("#fde725")));
    // Above range → bin N-1.
    QCOMPARE(r.colorForValue(50.0), QColor(QStringLiteral("#fde725")));
    // NaN → bin 0 (defensive default).
    QCOMPARE(r.colorForValue(std::nan("1")), QColor(QStringLiteral("#440154")));
}

void TestIFeatureRenderer::graduatedRenderer_legendItemsMatchBins()
{
    GraduatedRenderer r;
    r.setClassifyAttribute(QStringLiteral("depth"));
    r.setRange(0.0, 4.0);
    r.setBinColors({
        QColor(QStringLiteral("#440154")),
        QColor(QStringLiteral("#3b528b")),
        QColor(QStringLiteral("#21918c")),
        QColor(QStringLiteral("#fde725"))
    });
    r.setBaseSymbol(makeMarker(QStringLiteral("#000000")));

    const QList<LegendSymbolItem> items = r.legendSymbolItems();
    QCOMPARE(items.size(), 4);

    // Bin edges are evenly spaced over [0, 4].
    QCOMPARE(items.at(0).range.first, 0.0);
    QCOMPARE(items.at(0).range.second, 1.0);
    QCOMPARE(items.at(3).range.first, 3.0);
    QCOMPARE(items.at(3).range.second, 4.0);  // last bin closes exactly at maxValue

    // Each swatch carries the matching per-bin colour (override applied).
    // QColor::name(QColor::HexArgb) emits lowercase hex — the literal "#ff…"
    // here matches that exactly; case in `setBinColors` input does not survive
    // round-trip through QColor.
    QCOMPARE(items.at(0).symbol.layers.at(0).props.value(QStringLiteral("color")).toString(),
             QStringLiteral("#ff440154"));
    QCOMPARE(items.at(3).symbol.layers.at(0).props.value(QStringLiteral("color")).toString(),
             QStringLiteral("#fffde725"));

    // sortIndex set in ascending order.
    QCOMPARE(items.at(0).sortIndex, 0);
    QCOMPARE(items.at(3).sortIndex, 3);
}

void TestIFeatureRenderer::graduatedRenderer_autoClassifyIgnoresNonFinite()
{
    GraduatedRenderer r;
    r.setRange(99.0, 100.0);  // sentinel — should be overwritten by autoClassify
    r.autoClassify(QVector<double>{
        std::nan("1"),
        std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        3.5,
        1.0,
        7.25
    });
    QCOMPARE(r.minValue(), 1.0);
    QCOMPARE(r.maxValue(), 7.25);

    // Empty sample → range untouched.
    GraduatedRenderer r2;
    r2.setRange(99.0, 100.0);
    r2.autoClassify({});
    QCOMPARE(r2.minValue(), 99.0);
    QCOMPARE(r2.maxValue(), 100.0);

    // All-NaN sample → range untouched.
    r2.autoClassify(QVector<double>{ std::nan("1"), std::nan("2") });
    QCOMPARE(r2.minValue(), 99.0);
    QCOMPARE(r2.maxValue(), 100.0);
}

void TestIFeatureRenderer::graduatedRenderer_autoClassifyConstantSampleWidens()
{
    GraduatedRenderer r;
    r.autoClassify(QVector<double>{ 5.0, 5.0, 5.0 });
    // The renderer invents a tiny window so binning is still defined.
    QVERIFY(r.minValue() < 5.0);
    QVERIFY(r.maxValue() > 5.0);
    QVERIFY(r.maxValue() - r.minValue() > 0.0);
}

void TestIFeatureRenderer::graduatedRenderer_symbolForOverridesColorInBaseSymbol()
{
    GraduatedRenderer r;
    r.setClassifyAttribute(QStringLiteral("flow"));
    r.setRange(0.0, 10.0);
    r.setBinColors({ QColor(QStringLiteral("#000000")),
                     QColor(QStringLiteral("#ffffff")) });
    // Base symbol's colour is some sentinel; the renderer must overwrite it.
    r.setBaseSymbol(makeMarker(QStringLiteral("#ff00ff"), 12.0));

    QVariantMap attrs;
    attrs.insert(QStringLiteral("flow"), 9.0);
    const SymbolStyle s = r.symbolFor({}, attrs);
    // Size from the template is preserved.
    QCOMPARE(s.layers.at(0).props.value(QStringLiteral("size")).toDouble(), 12.0);
    // Colour is overwritten to bin 1's white. (lowercase per QColor::HexArgb)
    QCOMPARE(s.layers.at(0).props.value(QStringLiteral("color")).toString(),
             QStringLiteral("#ffffffff"));

    // Attribute missing → template returned untouched (no override).
    const SymbolStyle smiss = r.symbolFor({}, {});
    QCOMPARE(smiss.layers.at(0).props.value(QStringLiteral("color")).toString(),
             QStringLiteral("#ff00ff"));
}

void TestIFeatureRenderer::graduatedRenderer_jsonRoundTrip()
{
    GraduatedRenderer in;
    in.setClassifyAttribute(QStringLiteral("head"));
    in.setRange(2.5, 17.75);
    in.setBinColors({ QColor(QStringLiteral("#440154")),
                      QColor(QStringLiteral("#21918c")),
                      QColor(QStringLiteral("#fde725")) });
    in.setBaseSymbol(makeMarker(QStringLiteral("#000000")));

    GraduatedRenderer out;
    out.fromJson(in.toJson());

    QCOMPARE(out.classifyAttribute(), QStringLiteral("head"));
    QCOMPARE(out.minValue(), 2.5);
    QCOMPARE(out.maxValue(), 17.75);
    QCOMPARE(out.binCount(), 3);
    QCOMPARE(out.binColors().at(0), QColor(QStringLiteral("#FF440154")));
    QCOMPARE(out.binColors().at(2), QColor(QStringLiteral("#FFFDE725")));
    QCOMPARE(out.baseSymbol().layers.size(), 1);
}

void TestIFeatureRenderer::categorizedRenderer_paintAndFallback()
{
    CategorizedRenderer r;
    r.setClassifyAttribute(QStringLiteral("type"));
    r.addCategory({ QStringLiteral("JUNCTION"),
                    QStringLiteral("Junction"),
                    makeMarker(QStringLiteral("#1f77b4")) });
    r.addCategory({ QStringLiteral("STORAGE"),
                    QStringLiteral("Storage"),
                    makeMarker(QStringLiteral("#2ca02c")) });
    r.setFallbackSymbol(makeMarker(QStringLiteral("#7f7f7f")));

    QVariantMap a;
    a.insert(QStringLiteral("type"), QStringLiteral("STORAGE"));
    QCOMPARE(r.symbolFor({}, a).layers.at(0).props.value(QStringLiteral("color")).toString(),
             QStringLiteral("#2ca02c"));

    a.insert(QStringLiteral("type"), QStringLiteral("OUTFALL"));  // not registered
    QCOMPARE(r.symbolFor({}, a).layers.at(0).props.value(QStringLiteral("color")).toString(),
             QStringLiteral("#7f7f7f"));

    // legendSymbolItems has one row per category (not the fallback).
    QCOMPARE(r.legendSymbolItems().size(), 2);
    QCOMPARE(r.legendSymbolItems().at(0).label, QStringLiteral("Junction"));
}

void TestIFeatureRenderer::categorizedRenderer_jsonRoundTrip()
{
    CategorizedRenderer in;
    in.setClassifyAttribute(QStringLiteral("type"));
    in.addCategory({ QStringLiteral("JUNCTION"), QString(),
                     makeMarker(QStringLiteral("#1f77b4")) });
    in.setFallbackSymbol(makeMarker(QStringLiteral("#cccccc")));

    CategorizedRenderer out;
    out.fromJson(in.toJson());
    QCOMPARE(out.rendererId(), QStringLiteral("categorized"));
    QCOMPARE(out.classifyAttribute(), QStringLiteral("type"));
    QCOMPARE(out.categories().size(), 1);
    QCOMPARE(out.categories().first().value, QStringLiteral("JUNCTION"));
    // Empty label → still empty after round-trip (legend renders value as fallback).
    QCOMPARE(out.categories().first().label, QString());
}

void TestIFeatureRenderer::ruleBasedRenderer_emptyExpressionMatches()
{
    RuleBasedRenderer r;
    RuleBasedRenderer::Rule rule;
    rule.expression = QString();  // empty = matches always (stub behaviour)
    rule.label = QStringLiteral("Always");
    rule.symbol = makeMarker(QStringLiteral("#e377c2"));
    r.addRule(rule);
    r.setFallbackSymbol(makeMarker(QStringLiteral("#7f7f7f")));

    // First rule matches → its symbol returned.
    QCOMPARE(r.symbolFor({}, {}).layers.at(0).props.value(QStringLiteral("color")).toString(),
             QStringLiteral("#e377c2"));

    // Without rules, falls through to fallback.
    RuleBasedRenderer r2;
    r2.setFallbackSymbol(makeMarker(QStringLiteral("#000000")));
    QCOMPARE(r2.symbolFor({}, {}).layers.at(0).props.value(QStringLiteral("color")).toString(),
             QStringLiteral("#000000"));
}

void TestIFeatureRenderer::ruleBasedRenderer_jsonRoundTripWithScaleRange()
{
    RuleBasedRenderer in;
    RuleBasedRenderer::Rule rule;
    rule.expression = QStringLiteral("$depth > 5");
    rule.label = QStringLiteral("Deep");
    rule.symbol = makeMarker(QStringLiteral("#d62728"));
    rule.scaleRange = { 0.0001, 0.01 };
    in.addRule(rule);

    RuleBasedRenderer out;
    out.fromJson(in.toJson());
    QCOMPARE(out.rendererId(), QStringLiteral("rule"));
    QCOMPARE(out.rules().size(), 1);
    QCOMPARE(out.rules().first().expression, QStringLiteral("$depth > 5"));
    QCOMPARE(out.rules().first().label, QStringLiteral("Deep"));
    QCOMPARE(out.rules().first().scaleRange.first, 0.0001);
    QCOMPARE(out.rules().first().scaleRange.second, 0.01);
}

QTEST_MAIN(TestIFeatureRenderer)
#include "test_ifeaturerenderer.moc"
