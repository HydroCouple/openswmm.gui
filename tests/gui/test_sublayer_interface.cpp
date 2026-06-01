/*!
 * \file   test_sublayer_interface.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice S1 — verification for the ISublayer / SublayerStyle / SizeUnit
 *         foundation introduced by docs/RENDERING_OUTPUT_SUBLAYERS_PLAN.md.
 *
 *         Covers the four S1 acceptance criteria:
 *           1. Trivial ISublayer subclass compiles and reports identity /
 *              visibility / opacity / dynamic / kind via the abstract API.
 *           2. Trivial SublayerStyle subclass round-trips a Q_PROPERTY
 *              through the QObject metaobject (no QPropertyModel coupling
 *              here — that's the dialog's job in S3) and emits
 *              styleChanged() exactly once per logical edit.
 *           3. sceneSizeFromPixels / pixelsFromSceneSize give pixel-stable
 *              sizes at zooms 0.1× / 1× / 10× and tolerate the
 *              divide-by-zero edge case.
 *           4. LegendSymbolItem::sublayerId round-trips through JSON.
 */

#include <QJsonObject>
#include <QSignalSpy>
#include <QtTest/QtTest>

#include "render/isublayer.h"
#include "render/legendsymbolitem.h"
#include "render/screenpixels.h"
#include "render/sizeunit.h"
#include "render/sublayerstyle.h"

using namespace OpenSWMM::Render;

// ── Minimal SublayerStyle subclass for test 2 ────────────────────────────
class FakeStyle : public SublayerStyle
{
    Q_OBJECT
    Q_PROPERTY(double lineWidthPx READ lineWidthPx WRITE setLineWidthPx NOTIFY styleChanged)
    Q_CLASSINFO("group:lineWidthPx", "Symbology")

public:
    using SublayerStyle::SublayerStyle;

    [[nodiscard]] double lineWidthPx() const { return m_lineWidthPx; }
    void setLineWidthPx(double v)
    {
        if (qFuzzyCompare(m_lineWidthPx, v))
            return;
        m_lineWidthPx = v;
        setDirty();
    }

    [[nodiscard]] QJsonObject toJson() const override
    {
        return { { QStringLiteral("lineWidthPx"), m_lineWidthPx } };
    }
    void fromJson(const QJsonObject &j) override
    {
        m_lineWidthPx = j.value(QStringLiteral("lineWidthPx")).toDouble(1.0);
        setDirty();
    }

private:
    double m_lineWidthPx = 1.0;
};

// ── Minimal ISublayer subclass for test 1 ────────────────────────────────
class FakeSublayer : public ISublayer
{
    Q_OBJECT
public:
    explicit FakeSublayer(QObject *parent = nullptr)
        : ISublayer(parent), m_style(new FakeStyle(this))
    {
        connect(m_style, &SublayerStyle::styleChanged,
                this, &ISublayer::invalidated);
    }

    Kind kind() const override { return LineKind; }
    QString id() const override { return QStringLiteral("fake.conduit-line"); }
    QString displayName() const override { return QStringLiteral("Fake conduit line"); }

    bool  isVisible() const override { return m_visible; }
    void  setVisible(bool v) override
    {
        if (m_visible == v) return;
        m_visible = v;
        emit invalidated();
    }
    qreal opacity() const override { return m_opacity; }
    void  setOpacity(qreal o) override
    {
        if (qFuzzyCompare(m_opacity, o)) return;
        m_opacity = o;
        emit invalidated();
    }

    bool isDynamic() const override { return true; }

    SublayerStyle *style() override { return m_style; }

    QList<LegendSymbolItem> legendSymbolItems() const override
    {
        LegendSymbolItem it;
        it.label      = QStringLiteral("Fake row");
        it.sublayerId = id();
        return { it };
    }

    QSGNode *buildOrUpdateNode(QSGNode *, const SublayerContext &) override
    {
        return nullptr; // not exercised here — S1 does no GPU work
    }

private:
    bool  m_visible = true;
    qreal m_opacity = 1.0;
    FakeStyle *m_style;
};

// ── Test fixture ────────────────────────────────────────────────────────
class TestSublayerInterface : public QObject
{
    Q_OBJECT

private slots:
    // 1. ISublayer
    void sublayer_identity();
    void sublayer_visibility_signals();
    void sublayer_opacity_signals();
    void sublayer_legend_carries_sublayerId();

    // 2. SublayerStyle
    void style_property_roundTrip();
    void style_emits_exactly_once_per_change();
    void style_no_signal_on_unchanged_set();
    void style_json_roundTrip();

    // 3. Screen-pixel math
    void sceneSizeFromPixels_at_typical_zooms();
    void sceneSizeFromPixels_zero_scale_is_safe();
    void pixelsFromSceneSize_is_inverse();

    // 4. LegendSymbolItem.sublayerId JSON
    void legendItem_sublayerId_roundTrips();
    void legendItem_sublayerId_absent_when_empty();
};

// ── 1. ISublayer ────────────────────────────────────────────────────────
void TestSublayerInterface::sublayer_identity()
{
    FakeSublayer s;
    QCOMPARE(s.kind(),       ISublayer::LineKind);
    QCOMPARE(s.id(),         QStringLiteral("fake.conduit-line"));
    QCOMPARE(s.displayName(),QStringLiteral("Fake conduit line"));
    QCOMPARE(s.isVisible(),  true);
    QCOMPARE(s.opacity(),    1.0);
    QCOMPARE(s.isDynamic(),  true);
    QVERIFY(s.style() != nullptr);
}

void TestSublayerInterface::sublayer_visibility_signals()
{
    FakeSublayer s;
    QSignalSpy spy(&s, &ISublayer::invalidated);
    s.setVisible(false);
    QCOMPARE(spy.count(), 1);
    s.setVisible(false); // no-op
    QCOMPARE(spy.count(), 1);
    s.setVisible(true);
    QCOMPARE(spy.count(), 2);
}

void TestSublayerInterface::sublayer_opacity_signals()
{
    FakeSublayer s;
    QSignalSpy spy(&s, &ISublayer::invalidated);
    s.setOpacity(0.5);
    QCOMPARE(spy.count(), 1);
    s.setOpacity(0.5); // no-op
    QCOMPARE(spy.count(), 1);
}

void TestSublayerInterface::sublayer_legend_carries_sublayerId()
{
    FakeSublayer s;
    const auto items = s.legendSymbolItems();
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.first().sublayerId, s.id());
}

// ── 2. SublayerStyle ────────────────────────────────────────────────────
void TestSublayerInterface::style_property_roundTrip()
{
    FakeStyle st;
    st.setProperty("lineWidthPx", 4.0);
    QCOMPARE(st.property("lineWidthPx").toDouble(), 4.0);
    QCOMPARE(st.lineWidthPx(), 4.0);
}

void TestSublayerInterface::style_emits_exactly_once_per_change()
{
    FakeStyle st;
    QSignalSpy spy(&st, &SublayerStyle::styleChanged);
    st.setLineWidthPx(2.0);
    QCOMPARE(spy.count(), 1);
}

void TestSublayerInterface::style_no_signal_on_unchanged_set()
{
    FakeStyle st;
    st.setLineWidthPx(3.0);
    QSignalSpy spy(&st, &SublayerStyle::styleChanged);
    st.setLineWidthPx(3.0); // unchanged
    QCOMPARE(spy.count(), 0);
}

void TestSublayerInterface::style_json_roundTrip()
{
    FakeStyle a;
    a.setLineWidthPx(7.5);
    const QJsonObject j = a.toJson();

    FakeStyle b;
    b.fromJson(j);
    QCOMPARE(b.lineWidthPx(), 7.5);
}

// ── 3. Screen-pixel math ────────────────────────────────────────────────
void TestSublayerInterface::sceneSizeFromPixels_at_typical_zooms()
{
    // 6 px should map to 0.06 / 0.6 / 0.006 scene units at these zooms.
    QCOMPARE(sceneSizeFromPixels(6.0, 100.0),  0.06);
    QCOMPARE(sceneSizeFromPixels(6.0,  10.0),  0.6);
    QCOMPARE(sceneSizeFromPixels(6.0, 1000.0), 0.006);

    // Width = 0 px stays 0 at every zoom.
    QCOMPARE(sceneSizeFromPixels(0.0, 100.0), 0.0);
}

void TestSublayerInterface::sceneSizeFromPixels_zero_scale_is_safe()
{
    // Degenerate matrices (zoom = 0) must not crash. The contract is
    // "return 0 rather than divide by zero" — sublayers will draw
    // nothing rather than an infinite-radius blob.
    QCOMPARE(sceneSizeFromPixels(6.0, 0.0), 0.0);
    QCOMPARE(sceneSizeFromPixels(6.0, -1.0), 0.0); // negative scale also clamped
}

void TestSublayerInterface::pixelsFromSceneSize_is_inverse()
{
    constexpr double pps = 100.0;
    const double scene  = sceneSizeFromPixels(6.0, pps);
    QCOMPARE(pixelsFromSceneSize(scene, pps), 6.0);
}

// ── 4. LegendSymbolItem JSON ────────────────────────────────────────────
void TestSublayerInterface::legendItem_sublayerId_roundTrips()
{
    LegendSymbolItem a;
    a.label      = QStringLiteral("Depth 0–1 ft");
    a.sublayerId = QStringLiteral("depth-ramp");

    LegendSymbolItem b;
    b.fromJson(a.toJson());
    QCOMPARE(b.sublayerId, QStringLiteral("depth-ramp"));
}

void TestSublayerInterface::legendItem_sublayerId_absent_when_empty()
{
    // Items not produced by a sublayer must NOT emit the sublayerId key —
    // keeps existing .oswp / .swmm-style.json files clean and forward-
    // compatible. Verifies the symmetric "skip empty" behaviour the
    // sibling fields (userLabel, classKey) already use.
    LegendSymbolItem a;
    a.label = QStringLiteral("Single style");
    const QJsonObject j = a.toJson();
    QVERIFY(!j.contains(QStringLiteral("sublayerId")));
}

QTEST_MAIN(TestSublayerInterface)
#include "test_sublayer_interface.moc"
