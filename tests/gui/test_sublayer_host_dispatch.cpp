/*!
 * \file   test_sublayer_host_dispatch.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice S2.1 — verification for the ISublayerHost interface and its
 *         animation-tick dispatch contract.
 *
 *         Plan reference: RENDERING_OUTPUT_SUBLAYERS_PLAN.md §2 Decision 3
 *         ("only dynamic sublayers are invalidated by an animation tick").
 *
 *         Covers four acceptance criteria for S2.1:
 *           1. sublayers() returns the list the host was configured with,
 *              in insertion order.
 *           2. dispatchAnimationTick(period) invalidates ONLY sublayers
 *              whose isDynamic() returns true.
 *           3. Empty host is a no-op (no crash, no signals fired).
 *           4. Hidden sublayers (visible=false) still receive the
 *              invalidation — the host doesn't second-guess their state
 *              because a sublayer may choose to update internal caches
 *              even when not currently painting. Visibility filtering
 *              belongs to the renderer, not the dispatcher.
 */

#include <QSignalSpy>
#include <QtTest/QtTest>

#include "render/isublayer.h"
#include "render/isublayerhost.h"

using namespace OpenSWMM::Render;

// ── Minimal ISublayer subclass parameterised on isDynamic ────────────────
class FakeSublayer : public ISublayer
{
    Q_OBJECT
public:
    FakeSublayer(bool dynamic_, QString id_, QObject *parent = nullptr)
        : ISublayer(parent), m_dynamic(dynamic_), m_id(std::move(id_)) {}

    Kind    kind() const override        { return LineKind; }
    QString id() const override          { return m_id; }
    QString displayName() const override { return m_id; }

    bool  isVisible() const override     { return m_visible; }
    void  setVisible(bool v) override    { m_visible = v; }
    qreal opacity() const override       { return 1.0; }
    void  setOpacity(qreal) override     {}

    bool isDynamic() const override      { return m_dynamic; }

    SublayerStyle *style() override      { return nullptr; }

    QList<LegendSymbolItem> legendSymbolItems() const override { return {}; }
    QSGNode *buildOrUpdateNode(QSGNode *, const SublayerContext &) override { return nullptr; }

private:
    bool    m_dynamic;
    QString m_id;
    bool    m_visible = true;
};

// ── Minimal host owning a flat list ──────────────────────────────────────
class FakeHost : public ISublayerHost
{
public:
    void addSublayer(ISublayer *s) { m_list.append(s); }
    QList<ISublayer *> sublayers() const override { return m_list; }

private:
    QList<ISublayer *> m_list;
};

// ── Test fixture ─────────────────────────────────────────────────────────
class TestSublayerHostDispatch : public QObject
{
    Q_OBJECT

private slots:
    void empty_host_dispatch_is_a_noop();
    void sublayers_listed_in_insertion_order();
    void dispatchTick_invalidates_only_dynamic_sublayers();
    void dispatchTick_invalidates_hidden_dynamic_sublayers();
    void dispatchTick_passes_period_but_doesnt_require_consumption();
};

void TestSublayerHostDispatch::empty_host_dispatch_is_a_noop()
{
    FakeHost host;
    // Must not crash; sublayers() must be empty.
    host.dispatchAnimationTick(0);
    QCOMPARE(host.sublayers().size(), 0);
}

void TestSublayerHostDispatch::sublayers_listed_in_insertion_order()
{
    FakeHost host;
    FakeSublayer a(true,  QStringLiteral("a"));
    FakeSublayer b(false, QStringLiteral("b"));
    FakeSublayer c(true,  QStringLiteral("c"));
    host.addSublayer(&a);
    host.addSublayer(&b);
    host.addSublayer(&c);

    const auto list = host.sublayers();
    QCOMPARE(list.size(), 3);
    QCOMPARE(list[0]->id(), QStringLiteral("a"));
    QCOMPARE(list[1]->id(), QStringLiteral("b"));
    QCOMPARE(list[2]->id(), QStringLiteral("c"));
}

void TestSublayerHostDispatch::dispatchTick_invalidates_only_dynamic_sublayers()
{
    FakeHost host;
    FakeSublayer dyn(true,  QStringLiteral("dynamic"));
    FakeSublayer sta(false, QStringLiteral("static"));
    host.addSublayer(&dyn);
    host.addSublayer(&sta);

    QSignalSpy dynSpy(&dyn, &ISublayer::invalidated);
    QSignalSpy staSpy(&sta, &ISublayer::invalidated);

    host.dispatchAnimationTick(7);

    QCOMPARE(dynSpy.count(), 1);
    QCOMPARE(staSpy.count(), 0); // the perf-relevant assertion

    // A second tick is also one-emit-per-dynamic-sublayer.
    host.dispatchAnimationTick(8);
    QCOMPARE(dynSpy.count(), 2);
    QCOMPARE(staSpy.count(), 0);
}

void TestSublayerHostDispatch::dispatchTick_invalidates_hidden_dynamic_sublayers()
{
    // A dynamic sublayer that's currently hidden still wants the invalidation
    // so it can update its cached state if it has any (e.g. an isoline
    // sublayer that's toggled off may still want to extract contours for
    // the current period so the on-screen flash on re-show is instant).
    // Filtering by visible belongs to the renderer, not the dispatcher.
    FakeHost host;
    FakeSublayer hidden(true, QStringLiteral("hidden-dynamic"));
    hidden.setVisible(false);
    host.addSublayer(&hidden);

    QSignalSpy spy(&hidden, &ISublayer::invalidated);
    host.dispatchAnimationTick(0);
    QCOMPARE(spy.count(), 1);
}

void TestSublayerHostDispatch::dispatchTick_passes_period_but_doesnt_require_consumption()
{
    // The default base-class implementation ignores the period argument
    // (the QSG renderer reads the authoritative value from the
    // AnimationController when building SublayerContext). The signature
    // exists so an override CAN stash it for per-host caching. Verify
    // calling with any int is legal and does not affect behaviour.
    FakeHost host;
    FakeSublayer s(true, QStringLiteral("s"));
    host.addSublayer(&s);

    QSignalSpy spy(&s, &ISublayer::invalidated);
    host.dispatchAnimationTick(-1);
    host.dispatchAnimationTick(0);
    host.dispatchAnimationTick(1000000);
    QCOMPARE(spy.count(), 3);
}

QTEST_MAIN(TestSublayerHostDispatch)
#include "test_sublayer_host_dispatch.moc"
