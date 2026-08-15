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
// Gap A1.3 — archetype-seeded renderer factory.
#include "render/rendererfactory.h"

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
    // VS.4 — independent size + width output axes.
    void graduatedRenderer_widthAxisIndependentOfSize();
    void graduatedRenderer_widthAxisJsonRoundTrip();
    void graduatedRenderer_legacyMigratesSizeAxisToWidth();
    // P2 — static/dynamic source + range mode.
    void graduatedRenderer_sourceAndRangeModeRoundTrip();
    void graduatedRenderer_sourceAndRangeModeDefaults();
    void categorizedRenderer_paintAndFallback();
    void categorizedRenderer_jsonRoundTrip();
    void ruleBasedRenderer_emptyExpressionMatches();
    void ruleBasedRenderer_jsonRoundTripWithScaleRange();

    // Slice BB Phase 8.6.16 — per-class editing virtuals.
    void classEdit_singleSymbol_colorSizeWidthSymbol();
    void classEdit_singleSymbol_ignoresWrongKey();
    void classEdit_graduated_colorOverridesPersistThroughRampSwap();
    void classEdit_graduated_clearOverridesReverts();
    void classEdit_graduated_overridesRoundTripThroughJson();
    void classEdit_categorized_colorAndSymbolEditsByIndex();
    void classEdit_categorized_outOfRangeIndexIsNoOp();
    void classEdit_ruleBased_supportsClassEditReturnsFalse();

    // Gap A1.3 — archetype-seeded renderer factory.
    void factory_seedsArchetypeSkeletonPerArchetype();
    void factory_graduatedSymbolForHasColorForPointAndLine();
    void factory_carriesSymbolAndAttributeAcrossClassSwap();
    void factory_seedsAttributeFromFieldsByType();
    void factory_unknownIdReturnsNull();
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
    // X1 — colour props rehydrate as QColor variants on load (the in-memory
    // canonical form); compare colour VALUES, not string representations.
    QCOMPARE(out.props.value(QStringLiteral("color")).value<QColor>(),
             QColor(QStringLiteral("#1f77b4")));
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
    QCOMPARE(out.layers.at(1).props.value(QStringLiteral("color")).value<QColor>(),
             QColor(QStringLiteral("#17becf")));
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
    QCOMPARE(out.symbol.layers.at(0).props.value(QStringLiteral("color")).value<QColor>(),
             QColor(QStringLiteral("#440154")));
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
    QCOMPARE(out.symbol().layers.at(0).props.value(QStringLiteral("color")).value<QColor>(),
             QColor(QStringLiteral("#2ca02c")));
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
    // X1 — the override writes QColor variants; compare colour VALUES.
    QCOMPARE(items.at(0).symbol.layers.at(0).props.value(QStringLiteral("color")).value<QColor>(),
             QColor(QStringLiteral("#440154")));
    QCOMPARE(items.at(3).symbol.layers.at(0).props.value(QStringLiteral("color")).value<QColor>(),
             QColor(QStringLiteral("#fde725")));

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
    // Colour is overwritten to bin 1's white. (Gap A1.2 — compare as
    // QColor via the tolerant read; the canonical in-memory encoding is a
    // QColor variant, not a hex string.)
    QCOMPARE(SymbolProps::readColor(s.layers.at(0).props,
                                    QStringLiteral("color")),
             QColor(QStringLiteral("#ffffff")));

    // Attribute missing → template returned untouched (no override).
    const SymbolStyle smiss = r.symbolFor({}, {});
    QCOMPARE(SymbolProps::readColor(smiss.layers.at(0).props,
                                    QStringLiteral("color")),
             QColor(QStringLiteral("#ff00ff")));
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

void TestIFeatureRenderer::graduatedRenderer_widthAxisIndependentOfSize()
{
    GraduatedRenderer r;
    r.setClassifyAttribute(QStringLiteral("flow"));
    r.setRange(0.0, 10.0);
    r.setBinColors({ QColor(QStringLiteral("#000000")),
                     QColor(QStringLiteral("#ffffff")) });   // 2 bins

    // Base symbol carries both a "size" slot (marker) and a "width" slot
    // (line) so we can prove the two axes drive them independently.
    SymbolStyle base;
    SymbolLayer sl;
    sl.kind = SymbolLayerKind::SimpleLine;
    sl.props.insert(QStringLiteral("color"), QStringLiteral("#000000"));
    sl.props.insert(QStringLiteral("size"),  8.0);
    sl.props.insert(QStringLiteral("width"), 1.0);
    base.layers.append(sl);
    r.setBaseSymbol(base);

    r.setOutputSizeEnabled(true);
    r.setOutputSizeRange(4.0, 20.0);
    r.setOutputWidthEnabled(true);
    r.setOutputWidthRange(0.5, 5.0);

    // Top bin (value 9 → bin 1 of 2): size hits its max, width hits its
    // max, and the two values differ — proving independence.
    QVariantMap hi;
    hi.insert(QStringLiteral("flow"), 9.0);
    const SymbolStyle shi = r.symbolFor({}, hi);
    QCOMPARE(shi.layers.at(0).props.value(QStringLiteral("size")).toDouble(),  20.0);
    QCOMPARE(shi.layers.at(0).props.value(QStringLiteral("width")).toDouble(),  5.0);

    // Bottom bin (value 1 → bin 0): both hit their minima.
    QVariantMap lo;
    lo.insert(QStringLiteral("flow"), 1.0);
    const SymbolStyle slo = r.symbolFor({}, lo);
    QCOMPARE(slo.layers.at(0).props.value(QStringLiteral("size")).toDouble(),  4.0);
    QCOMPARE(slo.layers.at(0).props.value(QStringLiteral("width")).toDouble(), 0.5);
}

void TestIFeatureRenderer::graduatedRenderer_widthAxisJsonRoundTrip()
{
    GraduatedRenderer in;
    in.setBinColors({ QColor(QStringLiteral("#000000")),
                      QColor(QStringLiteral("#ffffff")) });
    in.setOutputWidthEnabled(true);
    in.setOutputWidthRange(1.25, 7.5);

    GraduatedRenderer out;
    out.fromJson(in.toJson());
    QVERIFY(out.outputWidthEnabled());
    QCOMPARE(out.outputWidthMin(), 1.25);
    QCOMPARE(out.outputWidthMax(), 7.5);
}

void TestIFeatureRenderer::graduatedRenderer_legacyMigratesSizeAxisToWidth()
{
    // A pre-VS.4 project: the size axis was on and drove both size + width.
    GraduatedRenderer base;
    base.setBinColors({ QColor(QStringLiteral("#000000")),
                        QColor(QStringLiteral("#ffffff")) });
    base.setOutputSizeEnabled(true);
    base.setOutputSizeRange(3.0, 9.0);

    QJsonObject j = base.toJson();
    j.remove(QStringLiteral("outputWidthEnabled"));
    j.remove(QStringLiteral("outputWidthMin"));
    j.remove(QStringLiteral("outputWidthMax"));

    GraduatedRenderer out;
    out.fromJson(j);
    // Width axis is migrated from the size axis so old line widths survive.
    QVERIFY(out.outputWidthEnabled());
    QCOMPARE(out.outputWidthMin(), 3.0);
    QCOMPARE(out.outputWidthMax(), 9.0);
}

void TestIFeatureRenderer::graduatedRenderer_sourceAndRangeModeRoundTrip()
{
    using OpenSWMM::Render::AttributeSourceKind;
    using OpenSWMM::Render::RangeMode;
    GraduatedRenderer in;
    in.setBinColors({ QColor(QStringLiteral("#000000")),
                      QColor(QStringLiteral("#ffffff")) });
    in.setSourceKind(AttributeSourceKind::Dynamic);
    in.setRangeMode(RangeMode::PerFrameAutoStretch);

    GraduatedRenderer out;
    out.fromJson(in.toJson());
    QCOMPARE(out.sourceKind(), AttributeSourceKind::Dynamic);
    QCOMPARE(out.rangeMode(),  RangeMode::PerFrameAutoStretch);
}

void TestIFeatureRenderer::graduatedRenderer_sourceAndRangeModeDefaults()
{
    using OpenSWMM::Render::AttributeSourceKind;
    using OpenSWMM::Render::RangeMode;
    // A pre-P2 JSON object (no sourceKind / rangeMode keys) must default to
    // Static / FixedOverRun so old projects load unchanged.
    GraduatedRenderer base;
    base.setBinColors({ QColor(QStringLiteral("#000000")) });
    QJsonObject j = base.toJson();
    j.remove(QStringLiteral("sourceKind"));
    j.remove(QStringLiteral("rangeMode"));
    GraduatedRenderer out;
    out.fromJson(j);
    QCOMPARE(out.sourceKind(), AttributeSourceKind::Static);
    QCOMPARE(out.rangeMode(),  RangeMode::FixedOverRun);
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

// ────────────────────────────────────────────────────────────────────────
// Slice BB Phase 8.6.16 — per-class editing virtuals.
// ────────────────────────────────────────────────────────────────────────

void TestIFeatureRenderer::classEdit_singleSymbol_colorSizeWidthSymbol()
{
    SingleSymbolRenderer r{ makeMarker(QStringLiteral("#1f77b4"), 8.0),
                            QStringLiteral("Junctions") };

    // Renderer advertises support for all four kinds.
    QVERIFY(r.supportsClassEdit(ClassEditKind::Color));
    QVERIFY(r.supportsClassEdit(ClassEditKind::Size));
    QVERIFY(r.supportsClassEdit(ClassEditKind::Width));
    QVERIFY(r.supportsClassEdit(ClassEditKind::Symbol));

    // Colour edit writes through to every layer that has a colour slot.
    // (Gap A1.2 — compare as QColor; canonical encoding is a QColor variant.)
    r.setColorForClass(QStringLiteral("single"), QColor(QStringLiteral("#abcdef")));
    QCOMPARE(SymbolProps::readColor(r.symbol().layers.at(0).props,
                                    QStringLiteral("color")),
             QColor(QStringLiteral("#abcdef")));

    // Size edit updates the marker size.
    r.setSizeForClass(QStringLiteral("single"), 17.0);
    QCOMPARE(r.symbol().layers.at(0).props.value(QStringLiteral("size")).toDouble(), 17.0);

    // Wholesale symbol replacement.
    r.setSymbolForClass(QStringLiteral("single"),
                        makeMarker(QStringLiteral("#000000"), 3.0));
    QCOMPARE(r.symbol().layers.at(0).props.value(QStringLiteral("size")).toDouble(), 3.0);
    QCOMPARE(r.symbol().layers.at(0).props.value(QStringLiteral("color")).toString(),
             QStringLiteral("#000000"));
}

void TestIFeatureRenderer::classEdit_singleSymbol_ignoresWrongKey()
{
    SingleSymbolRenderer r{ makeMarker(QStringLiteral("#1f77b4"), 8.0), {} };
    r.setColorForClass(QStringLiteral("0"), QColor(QStringLiteral("#ff0000")));    // wrong key
    r.setSizeForClass(QStringLiteral("bin-2"), 99.0);                              // wrong key
    QCOMPARE(r.symbol().layers.at(0).props.value(QStringLiteral("color")).toString(),
             QStringLiteral("#1f77b4"));
    QCOMPARE(r.symbol().layers.at(0).props.value(QStringLiteral("size")).toDouble(), 8.0);
}

void TestIFeatureRenderer::classEdit_graduated_colorOverridesPersistThroughRampSwap()
{
    GraduatedRenderer r;
    r.setRange(0.0, 10.0);
    r.setBaseSymbol(makeMarker(QStringLiteral("#000000")));

    // Override bin 2 to a specific colour.
    QVERIFY(r.supportsClassEdit(ClassEditKind::Color));
    QVERIFY(!r.supportsClassEdit(ClassEditKind::Size));
    r.setColorForClass(QStringLiteral("2"), QColor(QStringLiteral("#aabbcc")));
    QCOMPARE(r.colorForBin(2).name(QColor::HexRgb).toLower(),
             QStringLiteral("#aabbcc"));

    // Swap the ramp — bin 2 still resolves to the override.
    r.setRamp(RasterColorRamp::viridis(0.0, 10.0));
    QCOMPARE(r.colorForBin(2).name(QColor::HexRgb).toLower(),
             QStringLiteral("#aabbcc"));

    // Other bins still come from the ramp (not from the override).
    QVERIFY(r.colorForBin(0).name(QColor::HexRgb).toLower() != QStringLiteral("#aabbcc"));
}

void TestIFeatureRenderer::classEdit_graduated_clearOverridesReverts()
{
    GraduatedRenderer r;
    r.setRange(0.0, 10.0);
    r.setColorForClass(QStringLiteral("1"), QColor(QStringLiteral("#deadbe")));
    QCOMPARE(r.binColorOverrides().size(), 1);

    r.clearClassEditOverrides();
    QVERIFY(r.binColorOverrides().isEmpty());
    // Bin 1 colour now comes from the default Viridis ramp again.
    QVERIFY(r.colorForBin(1).name(QColor::HexRgb).toLower() != QStringLiteral("#deadbe"));
}

void TestIFeatureRenderer::classEdit_graduated_overridesRoundTripThroughJson()
{
    GraduatedRenderer src;
    src.setRange(0.0, 100.0);
    src.setColorForClass(QStringLiteral("0"), QColor(QStringLiteral("#112233")));
    src.setColorForClass(QStringLiteral("4"), QColor(QStringLiteral("#ff00ff")));

    GraduatedRenderer dst;
    dst.fromJson(src.toJson());

    QCOMPARE(dst.binColorOverrides().size(), 2);
    QCOMPARE(dst.colorForBin(0).name(QColor::HexRgb).toLower(),
             QStringLiteral("#112233"));
    QCOMPARE(dst.colorForBin(4).name(QColor::HexRgb).toLower(),
             QStringLiteral("#ff00ff"));
}

void TestIFeatureRenderer::classEdit_categorized_colorAndSymbolEditsByIndex()
{
    CategorizedRenderer r;
    r.setClassifyAttribute(QStringLiteral("kind"));
    r.addCategory({ QStringLiteral("conduit"), QStringLiteral("Conduit"),
                    makeMarker(QStringLiteral("#1f77b4")) });
    r.addCategory({ QStringLiteral("pump"),    QStringLiteral("Pump"),
                    makeMarker(QStringLiteral("#2ca02c")) });

    QVERIFY(r.supportsClassEdit(ClassEditKind::Color));
    QVERIFY(r.supportsClassEdit(ClassEditKind::Symbol));
    QVERIFY(!r.supportsClassEdit(ClassEditKind::Size));   // not editable here

    // Colour edit on index 1 mutates the second category's symbol.
    // (Gap A1.2 — compare as QColor; canonical encoding is a QColor variant.)
    r.setColorForClass(QStringLiteral("1"), QColor(QStringLiteral("#abcdef")));
    QCOMPARE(SymbolProps::readColor(r.categories().at(1).symbol.layers.at(0).props,
                                    QStringLiteral("color")),
             QColor(QStringLiteral("#abcdef")));
    // Index 0 is untouched.
    QCOMPARE(SymbolProps::readColor(r.categories().at(0).symbol.layers.at(0).props,
                                    QStringLiteral("color")),
             QColor(QStringLiteral("#1f77b4")));

    // Wholesale symbol replacement on index 0.
    r.setSymbolForClass(QStringLiteral("0"),
                        makeMarker(QStringLiteral("#000000"), 99.0));
    QCOMPARE(r.categories().at(0).symbol.layers.at(0).props.value(QStringLiteral("size")).toDouble(),
             99.0);
}

void TestIFeatureRenderer::classEdit_categorized_outOfRangeIndexIsNoOp()
{
    CategorizedRenderer r;
    r.addCategory({ QStringLiteral("a"), {}, makeMarker(QStringLiteral("#1f77b4")) });

    // Out-of-range index, non-int key, and invalid colour all silently no-op.
    r.setColorForClass(QStringLiteral("5"),     QColor(QStringLiteral("#ff0000")));
    r.setColorForClass(QStringLiteral("apple"), QColor(QStringLiteral("#ff0000")));
    r.setColorForClass(QStringLiteral("0"),     QColor());

    QCOMPARE(r.categories().at(0).symbol.layers.at(0).props.value(QStringLiteral("color")).toString(),
             QStringLiteral("#1f77b4"));
}

void TestIFeatureRenderer::classEdit_ruleBased_supportsClassEditReturnsFalse()
{
    // RuleBasedRenderer doesn't override the virtuals — interface default
    // returns false for every kind, and the setters are no-ops.
    RuleBasedRenderer r;
    QVERIFY(!r.supportsClassEdit(ClassEditKind::Color));
    QVERIFY(!r.supportsClassEdit(ClassEditKind::Size));
    QVERIFY(!r.supportsClassEdit(ClassEditKind::Width));
    QVERIFY(!r.supportsClassEdit(ClassEditKind::Symbol));
    // No-op setters shouldn't crash or change anything observable.
    r.setColorForClass(QStringLiteral("0"), QColor(QStringLiteral("#ff0000")));
    r.setSymbolForClass(QStringLiteral("0"), SymbolStyle{});
}

// ── Gap A1.3 — archetype-seeded renderer factory ───────────────────────

void TestIFeatureRenderer::factory_seedsArchetypeSkeletonPerArchetype()
{
    using FeatureSublayer = OpenSWMM::Render::FeatureSublayer;

    // Point → SimpleMarker skeleton with a fillColor slot.
    const SymbolStyle pt = RendererFactory::defaultSymbolForArchetype(
        FeatureSublayer::Archetype::Point);
    QCOMPARE(pt.layers.size(), 1);
    QCOMPARE(pt.layers.first().kind, SymbolLayerKind::SimpleMarker);
    QVERIFY(pt.layers.first().props.contains(QStringLiteral("fillColor")));
    QVERIFY(pt.layers.first().props.contains(QStringLiteral("size")));

    // Line → SimpleLine skeleton with a color + width slot.
    const SymbolStyle ln = RendererFactory::defaultSymbolForArchetype(
        FeatureSublayer::Archetype::Line);
    QCOMPARE(ln.layers.first().kind, SymbolLayerKind::SimpleLine);
    QVERIFY(ln.layers.first().props.contains(QStringLiteral("color")));
    QVERIFY(ln.layers.first().props.contains(QStringLiteral("width")));

    // Polygon → SimpleFill skeleton with fill + outline slots.
    const SymbolStyle pg = RendererFactory::defaultSymbolForArchetype(
        FeatureSublayer::Archetype::Polygon);
    QCOMPARE(pg.layers.first().kind, SymbolLayerKind::SimpleFill);
    QVERIFY(pg.layers.first().props.contains(QStringLiteral("fillColor")));
    QVERIFY(pg.layers.first().props.contains(QStringLiteral("outlineColor")));
}

void TestIFeatureRenderer::factory_graduatedSymbolForHasColorForPointAndLine()
{
    using FeatureSublayer = OpenSWMM::Render::FeatureSublayer;

    // The historical failure: a default-constructed GraduatedRenderer has
    // an EMPTY base symbol, so symbolFor() returned a colour-less style and
    // paint fell back to the legacy ramp. The factory must prevent that for
    // BOTH points (fillColor grammar) and lines (color grammar).
    for (auto archetype : { FeatureSublayer::Archetype::Point,
                            FeatureSublayer::Archetype::Line }) {
        auto r = RendererFactory::makeRenderer(QStringLiteral("graduated"),
                                               archetype);
        QVERIFY(r);
        auto *g = dynamic_cast<GraduatedRenderer *>(r.get());
        QVERIFY(g);
        g->setClassifyAttribute(QStringLiteral("depth"));
        g->autoClassify({ 0.0, 1.0, 2.0, 3.0, 4.0 });

        QVariantMap attrs;
        attrs.insert(QStringLiteral("depth"), 2.0);
        const SymbolStyle styled = g->symbolFor(FeatureRef{}, attrs);
        QVERIFY(!styled.layers.isEmpty());
        QVERIFY(SymbolProps::firstColor(styled).isValid());
    }
}

void TestIFeatureRenderer::factory_carriesSymbolAndAttributeAcrossClassSwap()
{
    using FeatureSublayer = OpenSWMM::Render::FeatureSublayer;

    // Author a distinctive single-symbol look…
    auto single = RendererFactory::makeRenderer(
        QStringLiteral("single"), FeatureSublayer::Archetype::Line);
    auto *ssr = dynamic_cast<SingleSymbolRenderer *>(single.get());
    QVERIFY(ssr);
    ssr->setColorForClass(QStringLiteral("single"), QColor(10, 20, 30));

    // …switch class to graduated: the base symbol must carry over.
    auto grad = RendererFactory::makeRenderer(
        QStringLiteral("graduated"), FeatureSublayer::Archetype::Line,
        single.get());
    auto *g = dynamic_cast<GraduatedRenderer *>(grad.get());
    QVERIFY(g);
    QCOMPARE(SymbolProps::firstColor(g->baseSymbol()), QColor(10, 20, 30));

    // …and a graduated → categorized swap carries the classify attribute.
    g->setClassifyAttribute(QStringLiteral("flow"));
    auto cat = RendererFactory::makeRenderer(
        QStringLiteral("categorized"), FeatureSublayer::Archetype::Line,
        grad.get());
    auto *c = dynamic_cast<CategorizedRenderer *>(cat.get());
    QVERIFY(c);
    QCOMPARE(c->classifyAttribute(), QStringLiteral("flow"));
    QCOMPARE(SymbolProps::firstColor(c->fallbackSymbol()), QColor(10, 20, 30));
}

void TestIFeatureRenderer::factory_seedsAttributeFromFieldsByType()
{
    using FeatureSublayer = OpenSWMM::Render::FeatureSublayer;
    using OpenSWMM::Render::AttributeField;

    QVector<AttributeField> fields;
    AttributeField tag;
    tag.name = QStringLiteral("tag");
    tag.type = QMetaType::QString;
    AttributeField depth;
    depth.name = QStringLiteral("depth");
    depth.type = QMetaType::Double;
    fields << tag << depth;

    // Graduated prefers the first NON-string field…
    auto grad = RendererFactory::makeRenderer(
        QStringLiteral("graduated"), FeatureSublayer::Archetype::Point,
        nullptr, &fields);
    QCOMPARE(dynamic_cast<GraduatedRenderer *>(grad.get())->classifyAttribute(),
             QStringLiteral("depth"));

    // …Categorized prefers the first string field.
    auto cat = RendererFactory::makeRenderer(
        QStringLiteral("categorized"), FeatureSublayer::Archetype::Point,
        nullptr, &fields);
    QCOMPARE(dynamic_cast<CategorizedRenderer *>(cat.get())->classifyAttribute(),
             QStringLiteral("tag"));
}

void TestIFeatureRenderer::factory_unknownIdReturnsNull()
{
    using FeatureSublayer = OpenSWMM::Render::FeatureSublayer;
    QVERIFY(!RendererFactory::makeRenderer(QStringLiteral("heatmap"),
                                           FeatureSublayer::Archetype::Point));
    QVERIFY(!RendererFactory::makeRenderer(QString(),
                                           FeatureSublayer::Archetype::Point));
}

QTEST_MAIN(TestIFeatureRenderer)
#include "test_ifeaturerenderer.moc"
