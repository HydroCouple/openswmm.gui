/**
 * @file      test_rendererpanelcontext.cpp
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Tests for Slice Z.3b — Rule-aware Rule::setRendererById.
 *
 *         The Z.3b Rule path is encapsulated as Rule::setRendererById —
 *         a stateless renderer-class swap that the SymbologyTab delegates
 *         to when the active RendererPanelContext has \c rule set. This
 *         test file probes \c setRendererById directly (clean Rule-only
 *         dependency surface) and verifies the RendererPanelContext.rule
 *         field default.
 *
 *         The SymbologyTab integration is verified via the build itself
 *         (compilation success) + manual smoke testing once the Rule
 *         widget is mounted into LayerStyleDialog (a Z.3c follow-up).
 *
 *         Success criterion (RENDERING_RULE_MODEL_PLAN.md §16, Z.3b):
 *           - RendererPanelContext.rule field defaults to nullptr.
 *           - setRendererById swaps to a fresh renderer of the named
 *             class.
 *           - Same-class is a no-op (returns false; renderer pointer
 *             unchanged).
 *           - Unknown / empty id is a no-op (returns false).
 *           - All 5 supported ids ("single" / "graduated" /
 *             "categorized" / "rule" / "unclassed") resolve correctly.
 *           - Successful swap emits rendererReplaced + ruleChanged.
 *           - JSON round-trip preserves the swapped renderer class.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#include <QSignalSpy>
#include <QtTest/QtTest>

#include "render/rule.h"
#include "ui/dialogs/irendererpanel.h"

using namespace OpenSWMM::Render;
using openswmmvis::ui::RendererPanelContext;

class TestRendererPanelContext : public QObject
{
    Q_OBJECT
private slots:
    // RendererPanelContext field defaults
    /**
     * @brief Verify a default renderer-panel context borrows no active Rule.
     * @note Pure value fixture, QCOMPARE nullptr, no widgets/renderer ownership transferred.
     */
    void context_ruleFieldDefaultsToNull();
    /**
     * @brief Verify a default renderer-panel context borrows no host layer.
     * @note Pure value fixture, QCOMPARE nullptr, no layer created or mutated.
     */
    void context_hostLayerFieldDefaultsToNull();
    /**
     * @brief Verify a default panel context has no optional category selection.
     * @note Checks has_value false on local context; no rendering or model lookup.
     */
    void context_categoryFieldDefaultsToEmpty();

    // Rule::setRendererById
    /**
     * @brief Verify changing a default single renderer to graduated reports a successful swap.
     * @note Asserts initial rendererId and true return on local Rule, no persistent layer edits.
     */
    void setById_returnsTrueOnSwap();
    /**
     * @brief Verify a categorized class swap replaces the renderer object and class identifier.
     * @note Old pointer retained only for comparison, never dereferenced after replacement; Rule
     * owns new renderer.
     */
    void setById_changesRendererIdentity();
    /**
     * @brief Verify selecting the current single class is a no-op preserving renderer identity.
     * @note Checks false return and identical borrowed pointer on local Rule, no file or GUI
     * effects.
     */
    void setById_sameClassReturnsFalseAndKeepsRenderer();
    /**
     * @brief Verify an unregistered class ID refuses without replacing the current renderer.
     * @note Checks false return and pointer preservation, no factory side effects asserted
     * beyond local Rule.
     */
    void setById_unknownIdReturnsFalse();
    /**
     * @brief Verify an empty renderer class ID refuses on a fresh Rule.
     * @note Checks false return; local ownership only, no settings/model files.
     */
    void setById_emptyIdReturnsFalse();
    /**
     * @brief Verify sequential swaps resolve graduated, categorized, rule, unclassed and single
     * IDs.
     * @note Every differing class must return true and expose requested rendererId; local Rule
     * owns replacements.
     */
    void setById_supportsAllFiveBuiltinIds();
    /**
     * @brief Verify one successful graduated swap emits exactly one replacement and one
     * rule-change signal.
     * @note QSignalSpy observes local Rule synchronously, no queued UI integration or external
     * mutation.
     */
    void setById_emitsRendererReplacedAndRuleChanged();
    /**
     * @brief Verify same/unknown class selections do not emit replacement notifications.
     * @note Same-class keeps both signal counts zero; later unknown class keeps replacement
     * count zero.
     * Uses local Rule/QSignalSpy only, does not claim broader UI behavior.
     */
    void setById_noOpDoesNotEmitSignals();
    /**
     * @brief Verify a Rule swapped to unclassed retains that renderer class through JSON
     * serialization.
     * @note Owned reconstructed Rule must exist and report unclassed; no settings file write or
     * source mutation.
     */
    void setById_roundTripsThroughJson();
};

// ── RendererPanelContext field defaults ────────────────────────────

void TestRendererPanelContext::context_ruleFieldDefaultsToNull()
{
    RendererPanelContext ctx;
    QCOMPARE(ctx.rule, nullptr);
}

void TestRendererPanelContext::context_hostLayerFieldDefaultsToNull()
{
    RendererPanelContext ctx;
    QCOMPARE(ctx.hostLayer, nullptr);
}

void TestRendererPanelContext::context_categoryFieldDefaultsToEmpty()
{
    RendererPanelContext ctx;
    QVERIFY(!ctx.category.has_value());
}

// ── Rule::setRendererById ──────────────────────────────────────────

void TestRendererPanelContext::setById_returnsTrueOnSwap()
{
    Rule r;
    QCOMPARE(r.renderer()->rendererId(), QStringLiteral("single"));
    QCOMPARE(r.setRendererById(QStringLiteral("graduated")), true);
}

void TestRendererPanelContext::setById_changesRendererIdentity()
{
    Rule r;
    IFeatureRenderer *before = r.renderer();
    r.setRendererById(QStringLiteral("categorized"));
    QVERIFY(r.renderer() != before);
    QCOMPARE(r.renderer()->rendererId(), QStringLiteral("categorized"));
}

void TestRendererPanelContext::setById_sameClassReturnsFalseAndKeepsRenderer()
{
    Rule r;
    IFeatureRenderer *before = r.renderer();
    QCOMPARE(r.setRendererById(QStringLiteral("single")), false);
    QCOMPARE(r.renderer(), before);
}

void TestRendererPanelContext::setById_unknownIdReturnsFalse()
{
    Rule r;
    IFeatureRenderer *before = r.renderer();
    QCOMPARE(r.setRendererById(QStringLiteral("noSuchClass")), false);
    QCOMPARE(r.renderer(), before);
}

void TestRendererPanelContext::setById_emptyIdReturnsFalse()
{
    Rule r;
    QCOMPARE(r.setRendererById(QString()), false);
}

void TestRendererPanelContext::setById_supportsAllFiveBuiltinIds()
{
    Rule r;
    const QStringList ids = {
        QStringLiteral("graduated"), QStringLiteral("categorized"), QStringLiteral("rule"),
        QStringLiteral("unclassed"), QStringLiteral("single"),
    };
    for (const QString &id : ids) {
        QVERIFY2(r.setRendererById(id), qPrintable(id));
        QCOMPARE(r.renderer()->rendererId(), id);
    }
}

void TestRendererPanelContext::setById_emitsRendererReplacedAndRuleChanged()
{
    Rule r;
    QSignalSpy replaced(&r, &Rule::rendererReplaced);
    QSignalSpy changed(&r, &Rule::ruleChanged);
    QVERIFY(r.setRendererById(QStringLiteral("graduated")));
    QCOMPARE(replaced.count(), 1);
    QCOMPARE(changed.count(), 1);
}

void TestRendererPanelContext::setById_noOpDoesNotEmitSignals()
{
    Rule r;
    QSignalSpy replaced(&r, &Rule::rendererReplaced);
    QSignalSpy changed(&r, &Rule::ruleChanged);
    // Same class — returns false, no signals.
    QVERIFY(!r.setRendererById(QStringLiteral("single")));
    QCOMPARE(replaced.count(), 0);
    QCOMPARE(changed.count(), 0);
    // Unknown id — same.
    QVERIFY(!r.setRendererById(QStringLiteral("noSuch")));
    QCOMPARE(replaced.count(), 0);
}

void TestRendererPanelContext::setById_roundTripsThroughJson()
{
    Rule r(QStringLiteral("swapped"), nullptr);
    QVERIFY(r.setRendererById(QStringLiteral("unclassed")));

    auto back = Rule::fromJson(r.toJson());
    QVERIFY(back);
    QCOMPARE(back->renderer()->rendererId(), QStringLiteral("unclassed"));
}

QTEST_MAIN(TestRendererPanelContext)
#include "test_rendererpanelcontext.moc"
