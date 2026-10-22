/**
 * @file      test_symbolstyleadapter.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Tests for Slice B.6c — SymbolStyleAdapter.
 *
 *         Success criterion: each Q_PROPERTY reads / writes through the
 *         Rule's SingleSymbolRenderer SymbolStyle correctly; setters
 *         emit the coalesced `changed` signal; external renderer swap
 *         (rendererReplaced) fires `changed` too so editors refresh.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include <QColor>
#include <QSignalSpy>
#include <QtTest/QtTest>

#include "render/renderers/graduatedrenderer.h"
#include "render/renderers/singlesymbolrenderer.h"
#include "render/rule.h"
#include "render/symbollayer.h"
#include "render/symbolstyle.h"
#include "render/symbolstyleadapter.h"

using namespace OpenSWMM::Render;

class TestSymbolStyleAdapter : public QObject
{
    Q_OBJECT
private slots:
    // Opacity
    /**
     *
     * @brief Require adapter opacity to read the underlying single-symbol style value .55.
     *
     */
    void opacity_readsRendererSymbolOpacity();
    /**
     *
     * @brief Require adapter opacity writes above/below [0,1] to clamp to 1/0.
     *
     */
    void opacity_writeClampsAndPersists();
    /**
     *
     * @brief Require an identical opacity write to emit no changed signal.
     *
     */
    void opacity_idempotentNoSignal();

    // Color writers
    /**
     *
     * @brief Require an adapter fill-color write to read back unchanged.
     *
     */
    void fillColor_roundTripsThroughProps();
    /**
     *
     * @brief Require stroke-color write to populate both color and outlineColor keys.
     *
     */
    void strokeColor_writesBothCanonicalKeys();
    /**
     *
     * @brief Require a style with only the line color key to supply adapter stroke color.
     * This case does not separately exercise outline-only fallback.
     *
     */
    void strokeColor_readsLineColorOrOutlineColor();

    // Width
    /**
     *
     * @brief Require stroke width 2.5 logical pixels to populate width and outlineWidth.
     *
     */
    void strokeWidth_writesBothWidthAndOutlineWidth();
    /**
     *
     * @brief Require a line-width-only property to supply adapter stroke width.
     * This case does not separately exercise outline-only precedence/fallback.
     *
     */
    void strokeWidth_readsLineWidthFirstThenOutline();

    // Marker
    /**
     *
     * @brief Require marker logical-pixel size write/read to preserve 12.
     *
     */
    void markerSize_roundTrips();
    /**
     *
     * @brief Require typed Cross shape setter/readback to agree; the public setter accepts
     * MarkerShape.
     *
     */
    void markerShape_roundTripsAsInt();

    // Signal behavior
    /**
     *
     * @brief Require three distinct adapter writes to emit at least three changed notifications.
     *
     */
    void changed_emittedOnEachWrite();
    /**
     *
     * @brief Require replacing the rule renderer with GraduatedRenderer to notify adapter
     * observers.
     *
     */
    void changed_emittedOnRendererReplaced();
    /**
     *
     * @brief Require opacity/size writes against a graduated renderer to emit no adapter changed
     * signal.
     *
     */
    void changed_notEmittedWhenRendererIsNotSingleSymbol();

    // Robustness
    /**
     *
     * @brief Require empty-layer single-symbol style to retain opacity and default
     * invalid-fill/zero-size/Circle reads.
     *
     */
    void readsReturnDefaultsForEmptySymbol();
    /**
     *
     * @brief Require a marker-size write against a graduated renderer to leave the adapter size
     * fallback zero.
     *
     */
    void writesNoOpWhenRendererIsNotSingleSymbol();
};

// ── Opacity ────────────────────────────────────────────────────────

void TestSymbolStyleAdapter::opacity_readsRendererSymbolOpacity()
{
    auto renderer = std::make_unique<SingleSymbolRenderer>();
    SymbolStyle s = renderer->symbol();
    s.opacity = 0.55;
    renderer->setSymbol(s);

    Rule rule(QStringLiteral("r"), std::move(renderer));
    SymbolStyleAdapter adapter(&rule);
    QCOMPARE(adapter.opacity(), 0.55);
}

void TestSymbolStyleAdapter::opacity_writeClampsAndPersists()
{
    Rule rule;
    SymbolStyleAdapter adapter(&rule);
    adapter.setOpacity(2.0);
    QCOMPARE(adapter.opacity(), 1.0);
    adapter.setOpacity(-0.5);
    QCOMPARE(adapter.opacity(), 0.0);
}

void TestSymbolStyleAdapter::opacity_idempotentNoSignal()
{
    Rule rule;
    SymbolStyleAdapter adapter(&rule);
    adapter.setOpacity(0.75);
    QSignalSpy spy(&adapter, &SymbolStyleAdapter::changed);
    adapter.setOpacity(0.75); // same value
    QCOMPARE(spy.count(), 0);
}

// ── Colors ─────────────────────────────────────────────────────────

void TestSymbolStyleAdapter::fillColor_roundTripsThroughProps()
{
    Rule rule;
    SymbolStyleAdapter adapter(&rule);
    adapter.setFillColor(QColor(50, 150, 200));
    QCOMPARE(adapter.fillColor(), QColor(50, 150, 200));
}

void TestSymbolStyleAdapter::strokeColor_writesBothCanonicalKeys()
{
    Rule rule;
    SymbolStyleAdapter adapter(&rule);
    adapter.setStrokeColor(QColor(10, 20, 30));
    // After write, both "color" and "outlineColor" should be set.
    auto *ssr = dynamic_cast<SingleSymbolRenderer *>(rule.renderer());
    QVERIFY(ssr);
    QVERIFY(!ssr->symbol().layers.isEmpty());
    const auto &props = ssr->symbol().layers.first().props;
    QCOMPARE(props.value(QStringLiteral("color")).value<QColor>(), QColor(10, 20, 30));
    QCOMPARE(props.value(QStringLiteral("outlineColor")).value<QColor>(), QColor(10, 20, 30));
}

void TestSymbolStyleAdapter::strokeColor_readsLineColorOrOutlineColor()
{
    // Seed with only "color" set (line-style props).
    auto renderer = std::make_unique<SingleSymbolRenderer>();
    SymbolStyle s = renderer->symbol();
    SymbolLayer layer;
    layer.props[QStringLiteral("color")] = QColor(100, 100, 100);
    s.layers.append(layer);
    renderer->setSymbol(s);

    Rule rule(QStringLiteral("r"), std::move(renderer));
    SymbolStyleAdapter adapter(&rule);
    QCOMPARE(adapter.strokeColor(), QColor(100, 100, 100));
}

// ── Width ──────────────────────────────────────────────────────────

void TestSymbolStyleAdapter::strokeWidth_writesBothWidthAndOutlineWidth()
{
    Rule rule;
    SymbolStyleAdapter adapter(&rule);
    adapter.setStrokeWidth(2.5);
    auto *ssr = dynamic_cast<SingleSymbolRenderer *>(rule.renderer());
    QVERIFY(ssr);
    QVERIFY(!ssr->symbol().layers.isEmpty());
    const auto &props = ssr->symbol().layers.first().props;
    QCOMPARE(props.value(QStringLiteral("width")).toDouble(), 2.5);
    QCOMPARE(props.value(QStringLiteral("outlineWidth")).toDouble(), 2.5);
}

void TestSymbolStyleAdapter::strokeWidth_readsLineWidthFirstThenOutline()
{
    auto renderer = std::make_unique<SingleSymbolRenderer>();
    SymbolStyle s = renderer->symbol();
    SymbolLayer layer;
    layer.props[QStringLiteral("width")] = 1.5;
    s.layers.append(layer);
    renderer->setSymbol(s);

    Rule rule(QStringLiteral("r"), std::move(renderer));
    SymbolStyleAdapter adapter(&rule);
    QCOMPARE(adapter.strokeWidth(), 1.5);
}

// ── Marker ─────────────────────────────────────────────────────────

void TestSymbolStyleAdapter::markerSize_roundTrips()
{
    Rule rule;
    SymbolStyleAdapter adapter(&rule);
    adapter.setMarkerSize(12.0);
    QCOMPARE(adapter.markerSize(), 12.0);
}

void TestSymbolStyleAdapter::markerShape_roundTripsAsInt()
{
    // Static-review fixup: MarkerShape is `enum class : int`, so the
    // setter takes a typed value rather than raw int (header
    // signature changed from int to MarkerShape during Slice Z.4
    // canonicalisation). The test still verifies the same round-trip
    // semantic.
    Rule rule;
    SymbolStyleAdapter adapter(&rule);
    adapter.setMarkerShape(MarkerShape::Cross);
    QCOMPARE(adapter.markerShape(), MarkerShape::Cross);
}

// ── Signals ────────────────────────────────────────────────────────

void TestSymbolStyleAdapter::changed_emittedOnEachWrite()
{
    Rule rule;
    SymbolStyleAdapter adapter(&rule);
    QSignalSpy spy(&adapter, &SymbolStyleAdapter::changed);
    adapter.setOpacity(0.5);
    adapter.setFillColor(QColor(Qt::red));
    adapter.setMarkerSize(10.0);
    QVERIFY(spy.count() >= 3);
}

void TestSymbolStyleAdapter::changed_emittedOnRendererReplaced()
{
    Rule rule;
    SymbolStyleAdapter adapter(&rule);
    QSignalSpy spy(&adapter, &SymbolStyleAdapter::changed);
    rule.setRenderer(std::make_unique<GraduatedRenderer>());
    QVERIFY(spy.count() >= 1);
}

void TestSymbolStyleAdapter::changed_notEmittedWhenRendererIsNotSingleSymbol()
{
    Rule rule(QStringLiteral("g"), std::make_unique<GraduatedRenderer>());
    SymbolStyleAdapter adapter(&rule);
    QSignalSpy spy(&adapter, &SymbolStyleAdapter::changed);
    // Writes are no-ops when the renderer isn't SingleSymbol.
    adapter.setOpacity(0.5);
    adapter.setMarkerSize(20.0);
    QCOMPARE(spy.count(), 0);
}

// ── Robustness ─────────────────────────────────────────────────────

void TestSymbolStyleAdapter::readsReturnDefaultsForEmptySymbol()
{
    // SingleSymbolRenderer with no SymbolLayer entries.
    auto renderer = std::make_unique<SingleSymbolRenderer>();
    SymbolStyle empty;
    empty.opacity = 0.42;
    renderer->setSymbol(empty);
    Rule rule(QStringLiteral("r"), std::move(renderer));
    SymbolStyleAdapter adapter(&rule);

    QCOMPARE(adapter.opacity(), 0.42); // opacity still readable
    QCOMPARE(adapter.fillColor(), QColor());
    QCOMPARE(adapter.markerSize(), 0.0);
    QCOMPARE(adapter.markerShape(), MarkerShape::Circle); // default fallback
}

void TestSymbolStyleAdapter::writesNoOpWhenRendererIsNotSingleSymbol()
{
    Rule rule(QStringLiteral("g"), std::make_unique<GraduatedRenderer>());
    SymbolStyleAdapter adapter(&rule);
    adapter.setMarkerSize(50.0);
    QCOMPARE(adapter.markerSize(), 0.0); // no SingleSymbol → no-op
}

QTEST_MAIN(TestSymbolStyleAdapter)
#include "test_symbolstyleadapter.moc"
