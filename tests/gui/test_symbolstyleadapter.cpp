/*!
 * \file   test_symbolstyleadapter.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Tests for Slice B.6c — SymbolStyleAdapter.
 *
 *         Success criterion: each Q_PROPERTY reads / writes through the
 *         Rule's SingleSymbolRenderer SymbolStyle correctly; setters
 *         emit the coalesced `changed` signal; external renderer swap
 *         (rendererReplaced) fires `changed` too so editors refresh.
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
    void opacity_readsRendererSymbolOpacity();
    void opacity_writeClampsAndPersists();
    void opacity_idempotentNoSignal();

    // Color writers
    void fillColor_roundTripsThroughProps();
    void strokeColor_writesBothCanonicalKeys();
    void strokeColor_readsLineColorOrOutlineColor();

    // Width
    void strokeWidth_writesBothWidthAndOutlineWidth();
    void strokeWidth_readsLineWidthFirstThenOutline();

    // Marker
    void markerSize_roundTrips();
    void markerShape_roundTripsAsInt();

    // Signal behavior
    void changed_emittedOnEachWrite();
    void changed_emittedOnRendererReplaced();
    void changed_notEmittedWhenRendererIsNotSingleSymbol();

    // Robustness
    void readsReturnDefaultsForEmptySymbol();
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
    adapter.setOpacity(0.75);  // same value
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
    QCOMPARE(props.value(QStringLiteral("color")).value<QColor>(),
             QColor(10, 20, 30));
    QCOMPARE(props.value(QStringLiteral("outlineColor")).value<QColor>(),
             QColor(10, 20, 30));
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

    QCOMPARE(adapter.opacity(), 0.42);   // opacity still readable
    QCOMPARE(adapter.fillColor(), QColor());
    QCOMPARE(adapter.markerSize(), 0.0);
    QCOMPARE(adapter.markerShape(), MarkerShape::Circle);  // default fallback
}

void TestSymbolStyleAdapter::writesNoOpWhenRendererIsNotSingleSymbol()
{
    Rule rule(QStringLiteral("g"), std::make_unique<GraduatedRenderer>());
    SymbolStyleAdapter adapter(&rule);
    adapter.setMarkerSize(50.0);
    QCOMPARE(adapter.markerSize(), 0.0);  // no SingleSymbol → no-op
}

QTEST_MAIN(TestSymbolStyleAdapter)
#include "test_symbolstyleadapter.moc"
