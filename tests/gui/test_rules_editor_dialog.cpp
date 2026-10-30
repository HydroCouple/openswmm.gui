/**
 * @file      test_rules_editor_dialog.cpp
 * @brief     Slice BR Phase 6.8.1/2 — focused tests for RulesEditorDialog,
 *         RuleSyntaxHighlighter, and RuleCodeEditor.
 *
 * Tests focus on the surface verifiable without instantiating a real
 * SWMMModelLayer (which has nanoflann + GDAL + scene-graph as transitive
 * deps). The widgets accept a null layer cleanly so we can exercise their
 * construction, signal wiring, and the vocabulary the completer + highlighter
 * pull from.
 *
 * End-to-end editor behaviour (open dialog → type rule → engine round-trip)
 * is verified by manual smoke of the running app once this slice lands; the
 * engine round-trip itself is pinned by the test_engine_control_rule_validate
 * binary.
 *
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 * @license   GPL-3.0-or-later
 */

#include <QApplication>
#include <QLineEdit>
#include <QListView>
#include <QSignalSpy>
#include <QStringList>
#include <QTest>
#include <QTextDocument>

#include "controls/rulesyntaxhighlighter.h"
#include "controls/rulevalidator.h"
#include "controls/controlruleprovider.h"
#include "ui/dialogs/ruleseditordialog.h"
#include "ui/models/rulelistmodel.h"
#include "ui/widgets/rulecodeeditor.h"

using openswmmvis::controls::RuleSyntaxHighlighter;
using openswmmvis::controls::RuleValidator;
using openswmmvis::controls::ControlRuleProvider;
using openswmmvis::controls::ValidationState;
using openswmmvis::ui::RuleCodeEditor;
using openswmmvis::ui::RulesEditorDialog;

class TestRulesEditorDialog : public QObject
{
    Q_OBJECT

private slots:

    // ── RuleSyntaxHighlighter vocabulary ────────────────────────────────

    /**
     *
     * @brief Require the block vocabulary to include RULE/IF/THEN/ELSE/AND/OR/PRIORITY.
     * Checks membership only, without token coloring, exact full-list equality or parsing.
     *
     */
    void highlighterVocabularyCoversKeywords()
    {
        const QStringList block = RuleSyntaxHighlighter::blockKeywords();
        QVERIFY(block.contains(QStringLiteral("RULE")));
        QVERIFY(block.contains(QStringLiteral("IF")));
        QVERIFY(block.contains(QStringLiteral("THEN")));
        QVERIFY(block.contains(QStringLiteral("ELSE")));
        QVERIFY(block.contains(QStringLiteral("AND")));
        QVERIFY(block.contains(QStringLiteral("OR")));
        QVERIFY(block.contains(QStringLiteral("PRIORITY")));
    }

    /**
     *
     * @brief Require representative node/link/pump/orifice/simulation and
     * depth/flow/status/setting vocabulary.
     * Checks static membership only, without object lookup, completion insertion or units.
     *
     */
    void highlighterVocabularyCoversObjectsAndVariables()
    {
        const QStringList obj = RuleSyntaxHighlighter::objectTypes();
        QVERIFY(obj.contains(QStringLiteral("NODE")));
        QVERIFY(obj.contains(QStringLiteral("LINK")));
        QVERIFY(obj.contains(QStringLiteral("PUMP")));
        QVERIFY(obj.contains(QStringLiteral("ORIFICE")));
        QVERIFY(obj.contains(QStringLiteral("SIMULATION")));

        const QStringList var = RuleSyntaxHighlighter::variableNames();
        QVERIFY(var.contains(QStringLiteral("DEPTH")));
        QVERIFY(var.contains(QStringLiteral("FLOW")));
        QVERIFY(var.contains(QStringLiteral("STATUS")));
        QVERIFY(var.contains(QStringLiteral("SETTING")));
    }

    /**
     *
     * @brief Require allKeywords to contain every block/object/variable vocabulary entry.
     * Does not exclude additional words or check duplicate freedom despite the case name.
     *
     */
    void highlighterAllKeywordsIsUnionOfThreeSets()
    {
        const QStringList all = RuleSyntaxHighlighter::allKeywords();
        for (const auto &kw : RuleSyntaxHighlighter::blockKeywords())
            QVERIFY(all.contains(kw));
        for (const auto &kw : RuleSyntaxHighlighter::objectTypes())
            QVERIFY(all.contains(kw));
        for (const auto &kw : RuleSyntaxHighlighter::variableNames())
            QVERIFY(all.contains(kw));
    }

    /**
     *
     * @brief Smoke-test highlighter construction on multiline rule text containing keywords,
     * numbers and a comment.
     * The sole explicit assertion is true; no text-format/color or explicit rehighlight
     * result is inspected.
     *
     */
    void highlighterAttachesToDocumentWithoutCrash()
    {
        QTextDocument doc;
        doc.setPlainText(QStringLiteral("RULE R1\n"
                                        "IF NODE J1 DEPTH > 5\n"
                                        "THEN PUMP P1 STATUS = ON\n"
                                        "; this is a comment\n"));
        RuleSyntaxHighlighter h(&doc);
        // The highlighter rebuilds formats from the default-constructed
        // palette during construction; no assertion required — we're
        // pinning that the constructor + rehighlight don't crash on
        // multi-line input including comments + numbers + keywords.
        QVERIFY(true);
    }

    // ── RuleCodeEditor — null layer ─────────────────────────────────────

    /**
     *
     * @brief Require default code-editor construction to create highlighter and completer
     * objects.
     * No layer binding, visible popup or keyboard interaction is exercised.
     *
     */
    void codeEditorConstructsWithoutLayer()
    {
        RuleCodeEditor ed;
        QVERIFY(ed.highlighter() != nullptr);
        QVERIFY(ed.completer() != nullptr);
    }

    /**
     *
     * @brief Require two-line rule text to round-trip exactly through the plain-text editor API.
     * Does not validate the incomplete rule, use a native engine or test syntax colors.
     *
     */
    void codeEditorAcceptsPlainText()
    {
        RuleCodeEditor ed;
        ed.setPlainText(QStringLiteral("RULE R\nIF NODE J1 DEPTH > 5"));
        QCOMPARE(ed.toPlainText(), QStringLiteral("RULE R\nIF NODE J1 DEPTH > 5"));
    }

    // ── RuleValidator — null layer falls back to Pending ────────────────

    /**
     *
     * @brief Require a nonempty rule draft to validate as Pending when no model layer is bound.
     * No native object lookup or syntax-verdict assertion.
     *
     */
    void validatorWithoutLayerIsPending()
    {
        RuleValidator v(nullptr);
        const auto r = v.validate(QStringLiteral("RULE R\nIF NODE J1 DEPTH > 5"));
        QCOMPARE(r.state, ValidationState::Pending);
    }

    /**
     *
     * @brief Require empty and whitespace-only drafts to remain Pending without a bound layer.
     * Does not check diagnostics or a live-engine validator path.
     *
     */
    void validatorEmptyTextIsPending()
    {
        RuleValidator v(nullptr);
        QCOMPARE(v.validate(QString()).state, ValidationState::Pending);
        QCOMPARE(v.validate(QStringLiteral("   \n\t  ")).state, ValidationState::Pending);
    }

    // ── RulesEditorDialog — null layer construction smoke test ──────────

    /**
     *
     * @brief Require null-layer dialog construction to supply list/editor/model with no rows or
     * selected provider.
     * No model is instantiated and the isolated-link stand-ins remain unused.
     *
     */
    void dialogConstructsWithoutLayer()
    {
        RulesEditorDialog dlg(nullptr, /*undoStack=*/nullptr);
        QVERIFY(dlg.listView() != nullptr);
        QVERIFY(dlg.codeEditor() != nullptr);
        QVERIFY(dlg.listModel() != nullptr);
        // Null layer → 0 rules, no current provider.
        QCOMPARE(dlg.listModel()->rowCount(), 0);
        QVERIFY(dlg.currentProvider() == nullptr);
    }

    /**
     *
     * @brief Require a null-layer rules dialog to default to Edit mode.
     * Does not check controls, provider binding or modal acceptance.
     *
     */
    void dialogModeDefaultIsEdit()
    {
        RulesEditorDialog dlg(nullptr, nullptr);
        QCOMPARE(dlg.mode(), RulesEditorDialog::Mode::Edit);
    }

    /**
     *
     * @brief Require invoking New without a layer/name to leave Create disabled.
     * Uses direct public action invocation; does not show the dialog, type a name or create
     * a native rule.
     *
     */
    void dialogInvokeNewSchedulesNoCrash()
    {
        // Even with a null layer, opening the create-card shouldn't crash.
        // The Create button stays disabled because the name field is empty
        // (which triggers the "Name required." validation).
        RulesEditorDialog dlg(nullptr, nullptr);
        dlg.invokeNew();
        QVERIFY(!dlg.isCreateEnabled());
    }
};

// ============================================================================
// Stub SWMMModelLayer surface — same idiom as test_control_rule_models /
// test_hydrograph_models. The dialog code paths exercised here pass nullptr
// for the layer, so these stubs are never invoked — they only need to
// satisfy the linker for the few member references inside ruleseditordialog.cpp
// / rulecodeeditor.cpp / rulevalidator.cpp.
// ============================================================================

#include "layers/swmmmodellayer.h"
#include <openswmm/engine/openswmm_engine.h>

/**
 *
 * @brief Return null from the unused isolated model-link stand-in.
 * @return Null borrowed handle; never a production engine accessor or allocator.
 *
 */
SWMM_Engine SWMMModelLayer::engine() const
{
    return nullptr;
}
/**
 *
 * @brief Return null from the unused isolated registry-link stand-in.
 * @return Null without lazy initialization, ownership transfer or model mutation.
 *
 */
QObject *SWMMModelLayer::ensureControlRuleRegistry()
{
    return nullptr;
}
/**
 *
 * @brief Refuse every add in the unused isolated-link stand-in.
 * @param name Borrowed ignored identity.
 * @param body Borrowed ignored rule text.
 * @param error Optional borrowed diagnostic destination, left unchanged.
 * @return Always false, with no native edit, signal or retained reference.
 *
 */
bool SWMMModelLayer::applyControlRuleAdd(const QString &name, const QString &body, QString *error)
{
    return false;
}
/**
 *
 * @brief Refuse every replacement in the unused isolated-link stand-in.
 * @param name Borrowed ignored identity.
 * @param body Borrowed ignored replacement text.
 * @param error Optional borrowed diagnostic destination, left unchanged.
 * @return Always false, with no native edit, signal or retained reference.
 *
 */
bool SWMMModelLayer::applyControlRuleReplace(const QString &name, const QString &body,
                                             QString *error)
{
    return false;
}
/**
 *
 * @brief Refuse every rename in the unused isolated-link stand-in.
 * @param oldName Borrowed ignored current identity.
 * @param newName Borrowed ignored requested identity.
 * @param error Optional borrowed diagnostic destination, left unchanged.
 * @return Always false, with no native edit, signal or retained reference.
 *
 */
bool SWMMModelLayer::applyControlRuleRename(const QString &oldName, const QString &newName,
                                            QString *error)
{
    return false;
}
/**
 *
 * @brief Refuse every removal in the unused isolated-link stand-in.
 * @param name Borrowed ignored identity.
 * @param error Optional borrowed diagnostic destination, left unchanged.
 * @return Always false, with no native edit, signal or retained reference.
 *
 */
bool SWMMModelLayer::applyControlRuleRemove(const QString &name, QString *error)
{
    return false;
}
/**
 *
 * @brief Report no category objects in the unused isolated-link stand-in.
 * @param category Ignored category enumeration.
 * @return Always zero, without consulting native topology or metadata.
 *
 */
int SWMMModelLayer::categoryCount(SWMMModelLayer::Category category) const
{
    return 0;
}
/**
 *
 * @brief Return no object name in the unused isolated-link stand-in.
 * @param category Ignored category enumeration.
 * @param row Ignored row, without range validation.
 * @return Owned empty string, without topology access or retained references.
 *
 */
QString SWMMModelLayer::objectNameAt(SWMMModelLayer::Category category, int row) const
{
    return {};
}

QTEST_MAIN(TestRulesEditorDialog)
#include "test_rules_editor_dialog.moc"
