/*!
 * \file   test_rendererpanelcontext.cpp
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Tests for Slice Z.3b — Rule-aware Rule::setRendererById.
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
    void context_ruleFieldDefaultsToNull();
    void context_hostLayerFieldDefaultsToNull();
    void context_categoryFieldDefaultsToEmpty();

    // Rule::setRendererById
    void setById_returnsTrueOnSwap();
    void setById_changesRendererIdentity();
    void setById_sameClassReturnsFalseAndKeepsRenderer();
    void setById_unknownIdReturnsFalse();
    void setById_emptyIdReturnsFalse();
    void setById_supportsAllFiveBuiltinIds();
    void setById_emitsRendererReplacedAndRuleChanged();
    void setById_noOpDoesNotEmitSignals();
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
        QStringLiteral("graduated"),
        QStringLiteral("categorized"),
        QStringLiteral("rule"),
        QStringLiteral("unclassed"),
        QStringLiteral("single"),
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
