/*!
 * \file   test_reaction_expression_editor.cpp
 * \brief  The reaction-expression editing stack: live model vocabulary in the
 *         completer, the drift guard against the engine grammar, and the
 *         debounced validation signal.
 *
 * \details `TRANSPORT_QUALITY_GUI_PLAN_2026-08-12.md:450` asked for a mirror
 *          of `test_treatment_expression_editor.cpp`. It was never written, so
 *          until now the newer and more capable of the two editors — the one
 *          the MSX Reaction System dialog hosts — had NO observer for its
 *          highlighter or its completer, while its older sibling had three.
 *
 *          **This is deliberately NOT a straight mirror**, because a straight
 *          mirror would be nearly vacuous here. `TreatmentSyntaxHighlighter`
 *          carries hardcoded name lists, so checking them against the engine
 *          is a real drift guard. `ReactionSyntaxHighlighter::hydVarNames()`
 *          and `functionNames()` already READ from the engine
 *          (`swmm_reaction_hydvar_*`, `swmm_reaction_function_*`), so
 *          re-validating them against that same engine asserts the engine
 *          against itself and would pass on a broken widget.
 *
 *          What can actually drift here is the half the treatment editor does
 *          not have: the **model** vocabulary. `modelIdentifiers()` publishes
 *          species + coefficients + terms + POLLUTANTS into the completer, and
 *          nothing has ever checked that the grammar accepts all four families
 *          as operands. That is gate 2, and it is the one that might fail.
 *
 *          No gate here writes a file, so there is no output directory and
 *          no fixture literal for the `b85b802d` collision guard to police.
 */

#include "ui/widgets/reactionexpressionedit.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_reactions.h>

#include <QAbstractItemModel>
#include <QCompleter>
#include <QObject>
#include <QSignalSpy>
#include <QStringList>
#include <QTest>

using openswmmvis::ui::ReactionExpressionEdit;
using openswmmvis::ui::ReactionSyntaxHighlighter;

namespace {

/*! BUILDING engine carrying one of every identifier family the completer
 *  publishes. The reaction CRUD mutators validate eagerly and roll back
 *  (D-RC5), so every add here is also an assertion that the system still
 *  compiles. */
SWMM_Engine makeEngine()
{
    SWMM_Engine e = swmm_engine_new();
    if (!e) return nullptr;
    swmm_node_add(e, "J0", 0 /*JUNCTION*/);
    swmm_link_add(e, "C1", 0 /*CONDUIT*/);
    swmm_pollutant_add(e, "TSS", 0 /*MG/L*/);
    return e;
}

QStringList completerWords(const ReactionExpressionEdit &edit)
{
    QStringList out;
    auto *c = edit.findChild<QCompleter *>();
    if (!c || !c->model()) return out;
    for (int i = 0; i < c->model()->rowCount(); ++i)
        out << c->model()->index(i, 0).data().toString();
    return out;
}

} // namespace

class TestReactionExpressionEditor : public QObject
{
    Q_OBJECT

private slots:

    /*! The context half: the completer's vocabulary is pulled from the LIVE
     *  model, and re-pulled on demand. A ctor-time snapshot would pass the
     *  first half of this and fail the second — which is the defect worth
     *  catching, because the dialog does CRUD and then calls
     *  refreshVocabulary(). */
    void completerTracksModelVocabulary()
    {
        SWMM_Engine e = makeEngine();
        QVERIFY(e);
        QCOMPARE(swmm_reaction_species_add(e, "AS3", 0, "MG", 0.0, 0.0),
                 SWMM_OK);
        QCOMPARE(swmm_reaction_coeff_add(e, "Kw", 0 /*CONSTANT*/, 1.0),
                 SWMM_OK);
        QCOMPARE(swmm_reaction_term_add(e, "Kf", "1.0"), SWMM_OK);

        ReactionExpressionEdit edit(e, SWMM_RXN_SCOPE_PIPE);
        QStringList words = completerWords(edit);
        QVERIFY2(!words.isEmpty(), "the completer published no vocabulary");
        QVERIFY2(words.contains(QStringLiteral("AS3")),
                 "a declared species is missing from the completer");
        QVERIFY2(words.contains(QStringLiteral("Kw")),
                 "a declared coefficient is missing from the completer");
        QVERIFY2(words.contains(QStringLiteral("Kf")),
                 "a declared term is missing from the completer");
        QVERIFY2(words.contains(QStringLiteral("TSS")),
                 "a declared pollutant is missing from the completer");

        // Added AFTER construction: the widget must re-pull, not serve a
        // snapshot taken in its constructor.
        QCOMPARE(swmm_reaction_species_add(e, "NH2CL", 0, "MG", 0.0, 0.0),
                 SWMM_OK);
        QVERIFY2(!completerWords(edit).contains(QStringLiteral("NH2CL")),
                 "the completer changed without refreshVocabulary() — this "
                 "gate can no longer tell a re-pull from a snapshot");
        edit.refreshVocabulary();
        QVERIFY2(completerWords(edit).contains(QStringLiteral("NH2CL")),
                 "refreshVocabulary() did not pick up a species added after "
                 "construction — the completer is a stale snapshot");

        swmm_engine_destroy(e);
    }

    /*! THE DRIFT GUARD, pointed where drift can actually happen. Every model
     *  identifier the completer offers must be an operand the engine's own
     *  validator accepts; otherwise the UI is advertising tokens the grammar
     *  rejects and the user learns that only after typing one.
     *
     *  ⚠ The POLLUTANT leg is untested ground. modelIdentifiers() publishes
     *  swmm_pollutant_* names alongside species/coeffs/terms, and nothing has
     *  ever checked that the reaction grammar resolves a pollutant as an
     *  operand. If this fails on TSS, that is a FINDING about the widget's
     *  vocabulary — do not delete the leg to make it green. Either the
     *  grammar should accept pollutants or the completer should stop
     *  offering them; both are real answers and the fix belongs with whoever
     *  owns that decision. */
    void completerAdvertisesOnlyIdentifiersTheGrammarAccepts()
    {
        SWMM_Engine e = makeEngine();
        QVERIFY(e);
        QCOMPARE(swmm_reaction_species_add(e, "AS3", 0, "MG", 0.0, 0.0),
                 SWMM_OK);
        QCOMPARE(swmm_reaction_coeff_add(e, "Kw", 0, 1.0), SWMM_OK);
        QCOMPARE(swmm_reaction_term_add(e, "Kf", "1.0"), SWMM_OK);

        ReactionExpressionEdit edit(e, SWMM_RXN_SCOPE_PIPE);

        // Only the MODEL identifiers are under test: hydvars and functions
        // are read straight out of the engine by hydVarNames()/functionNames(),
        // so asserting them here would be the engine agreeing with itself.
        QSet<QString> engineSourced;
        for (const QString &v : ReactionSyntaxHighlighter::hydVarNames())
            engineSourced.insert(v);
        for (const QString &f : ReactionSyntaxHighlighter::functionNames())
            engineSourced.insert(f);

        char err[256] = {};
        int  col = -1;
        for (const QString &w : completerWords(edit)) {
            if (engineSourced.contains(w)) continue;
            const QString expr = QStringLiteral("AS3 = %1").arg(w);
            const int rc = swmm_reaction_validate_expression(
                e, SWMM_RXN_SCOPE_PIPE, expr.toUtf8().constData(),
                err, sizeof(err), &col);
            QVERIFY2(rc == SWMM_OK,
                     qPrintable(QStringLiteral(
                         "the completer offers \"%1\" but the engine rejects "
                         "it as an operand: %2")
                         .arg(w, QString::fromUtf8(err))));
        }

        swmm_engine_destroy(e);
    }

    /*! Validation reaches the signal the hosting page's banner listens to,
     *  in both directions, with a column on failure. setExpression() calls
     *  validateNow() directly, so this needs no event loop and does not
     *  depend on the 250 ms debounce. */
    void validationReachesTheSignal()
    {
        SWMM_Engine e = makeEngine();
        QVERIFY(e);
        QCOMPARE(swmm_reaction_species_add(e, "AS3", 0, "MG", 0.0, 0.0),
                 SWMM_OK);

        ReactionExpressionEdit edit(e, SWMM_RXN_SCOPE_PIPE);
        QSignalSpy spy(&edit, &ReactionExpressionEdit::validationChanged);

        edit.setExpression(QStringLiteral("AS3 = -0.5 * AS3"));
        QVERIFY(spy.count() >= 1);
        QVERIFY2(spy.takeLast().at(0).toBool(),
                 "a well-formed rate expression was reported invalid");

        spy.clear();
        edit.setExpression(QStringLiteral("AS3 = NOT_A_SPECIES * 2"));
        auto bad = spy.takeLast();
        QVERIFY2(!bad.at(0).toBool(),
                 "an unknown identifier was reported valid");
        QVERIFY2(!bad.at(1).toString().isEmpty(),
                 "an invalid expression produced no diagnostic");

        // Empty clears the expression — a non-error, per the widget contract.
        spy.clear();
        edit.setExpression(QString());
        auto empty = spy.takeLast();
        QVERIFY(empty.at(0).toBool());
        QVERIFY(empty.at(1).toString().isEmpty());

        swmm_engine_destroy(e);
    }
};

QTEST_MAIN(TestReactionExpressionEditor)
#include "test_reaction_expression_editor.moc"
