/*!
 * \file   test_rules_editor_dialog.cpp
 * \brief  Slice BR Phase 6.8.1/2 — focused tests for RulesEditorDialog,
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

    void highlighterAttachesToDocumentWithoutCrash()
    {
        QTextDocument doc;
        doc.setPlainText(
            QStringLiteral("RULE R1\n"
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

    void codeEditorConstructsWithoutLayer()
    {
        RuleCodeEditor ed;
        QVERIFY(ed.highlighter() != nullptr);
        QVERIFY(ed.completer() != nullptr);
    }

    void codeEditorAcceptsPlainText()
    {
        RuleCodeEditor ed;
        ed.setPlainText(QStringLiteral("RULE R\nIF NODE J1 DEPTH > 5"));
        QCOMPARE(ed.toPlainText(),
                 QStringLiteral("RULE R\nIF NODE J1 DEPTH > 5"));
    }

    // ── RuleValidator — null layer falls back to Pending ────────────────

    void validatorWithoutLayerIsPending()
    {
        RuleValidator v(nullptr);
        const auto r = v.validate(QStringLiteral("RULE R\nIF NODE J1 DEPTH > 5"));
        QCOMPARE(r.state, ValidationState::Pending);
    }

    void validatorEmptyTextIsPending()
    {
        RuleValidator v(nullptr);
        QCOMPARE(v.validate(QString()).state, ValidationState::Pending);
        QCOMPARE(v.validate(QStringLiteral("   \n\t  ")).state, ValidationState::Pending);
    }

    // ── RulesEditorDialog — null layer construction smoke test ──────────

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

    void dialogModeDefaultIsEdit()
    {
        RulesEditorDialog dlg(nullptr, nullptr);
        QCOMPARE(dlg.mode(), RulesEditorDialog::Mode::Edit);
    }

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

SWMM_Engine SWMMModelLayer::engine() const { return nullptr; }
QObject *SWMMModelLayer::ensureControlRuleRegistry() { return nullptr; }
bool SWMMModelLayer::applyControlRuleAdd(const QString&, const QString&, QString*) { return false; }
bool SWMMModelLayer::applyControlRuleReplace(const QString&, const QString&, QString*) { return false; }
bool SWMMModelLayer::applyControlRuleRename(const QString&, const QString&, QString*) { return false; }
bool SWMMModelLayer::applyControlRuleRemove(const QString&, QString*) { return false; }
int SWMMModelLayer::categoryCount(SWMMModelLayer::Category) const { return 0; }
QString SWMMModelLayer::objectNameAt(SWMMModelLayer::Category, int) const { return {}; }

QTEST_MAIN(TestRulesEditorDialog)
#include "test_rules_editor_dialog.moc"
