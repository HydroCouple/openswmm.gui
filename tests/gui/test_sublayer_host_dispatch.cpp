/**
 * @file      test_sublayer_host_dispatch.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice S2.1 — verification for the ISublayerHost interface and its
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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
    /**
     *
     * @brief Construct a minimal borrowed-host fixture sublayer.
     * @param dynamic_ Time-varying flag returned verbatim by isDynamic.
     * @param id_ Owned identifier moved into the fixture, also used as display name.
     * @param parent Optional QObject owner; null leaves destruction to the caller.
     * No style, geometry, GPU resource or signal connection is allocated.
     *
     */
    FakeSublayer(bool dynamic_, QString id_, QObject *parent = nullptr)
        : ISublayer(parent), m_dynamic(dynamic_), m_id(std::move(id_))
    {
    }

    /**
     *
     * @brief Return the fake line category.
     * @return Always LineKind.
     *
     */
    Kind kind() const override { return LineKind; }
    /**
     *
     * @brief Read the stored fixture identifier.
     * @return By-value QString sharing Qt storage; no host lookup.
     *
     */
    QString id() const override { return m_id; }
    /**
     *
     * @brief Use the fixture identifier as its label.
     * @return By-value stored identifier, without localization.
     *
     */
    QString displayName() const override { return m_id; }

    /**
     *
     * @brief Read fixture visibility.
     * @return Stored flag, initially true.
     *
     */
    bool isVisible() const override { return m_visible; }
    /**
     *
     * @brief Store fake visibility without emitting invalidation.
     * @param v New visibility flag, used only for host-dispatch assertions.
     *
     */
    void setVisible(bool v) override { m_visible = v; }
    /**
     *
     * @brief Return fixed full opacity.
     * @return Always one; the matching fake setter deliberately does nothing.
     *
     */
    qreal opacity() const override { return 1.0; }
    /**
     *
     * @brief Ignore opacity edits in the dispatch-only fixture.
     * @param opacity Unused proposed alpha; no validation, state mutation or signals.
     *
     */
    void setOpacity(qreal opacity) override {}

    /**
     *
     * @brief Read the constructor-supplied dynamic flag.
     * @return True only for fixture entries intended to receive animation invalidation.
     *
     */
    bool isDynamic() const override { return m_dynamic; }

    /**
     *
     * @brief Report no style for the dispatch-only fixture.
     * @return nullptr; no object ownership is transferred.
     *
     */
    SublayerStyle *style() override { return nullptr; }

    /**
     *
     * @brief Report no legend entries for the dispatch-only fixture.
     * @return Empty owned list.
     *
     */
    QList<LegendSymbolItem> legendSymbolItems() const override { return {}; }
    /**
     *
     * @brief Perform no scene-graph work in the dispatch-only fixture.
     * @param node Unused borrowed old node, never deleted or updated here.
     * @param context Unused borrowed rendering context.
     * @return nullptr; tests supply no old resource needing disposal.
     *
     */
    QSGNode *buildOrUpdateNode(QSGNode *node, const SublayerContext &context) override
    {
        return nullptr;
    }

private:
    bool m_dynamic;
    QString m_id;
    bool m_visible = true;
};

// ── Minimal host owning a flat list ──────────────────────────────────────
class FakeHost : public ISublayerHost
{
public:
    /**
     *
     * @brief Append a borrowed fixture entry without taking ownership or connecting signals.
     * @param s Nonnull sublayer for dispatch use; must outlive host dispatch/list access.
     * Duplicates/nulls are not rejected here. Does not reparent or delete the entry.
     *
     */
    void addSublayer(ISublayer *s) { m_list.append(s); }
    /**
     *
     * @brief Return the host entries in insertion order.
     * @return Owned list of borrowed pointers; copying does not extend object lifetimes.
     *
     */
    QList<ISublayer *> sublayers() const override { return m_list; }

private:
    QList<ISublayer *> m_list;
};

// ── Test fixture ─────────────────────────────────────────────────────────
class TestSublayerHostDispatch : public QObject
{
    Q_OBJECT

private slots:
    /**
     *
     * @brief Require dispatch on an empty host to complete and leave its entry list empty.
     *
     */
    void empty_host_dispatch_is_a_noop();
    /**
     *
     * @brief Require three borrowed fixture entries to be returned in insertion order.
     *
     */
    void sublayers_listed_in_insertion_order();
    /**
     *
     * @brief Require one invalidation per dynamic entry per tick and none for the static entry.
     *
     */
    void dispatchTick_invalidates_only_dynamic_sublayers();
    /**
     *
     * @brief Require a hidden dynamic entry still to receive host animation invalidation.
     *
     */
    void dispatchTick_invalidates_hidden_dynamic_sublayers();
    /**
     *
     * @brief Require negative, zero and large period identifiers to each invalidate a dynamic
     * entry.
     * The default dispatch ignores period; this checks legal invocation and signal count,
     * not consumption of the identifier by a renderer.
     *
     */
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
    FakeSublayer a(true, QStringLiteral("a"));
    FakeSublayer b(false, QStringLiteral("b"));
    FakeSublayer c(true, QStringLiteral("c"));
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
    FakeSublayer dyn(true, QStringLiteral("dynamic"));
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
