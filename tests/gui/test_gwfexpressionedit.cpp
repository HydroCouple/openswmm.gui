/*!
 * \file   test_gwfexpressionedit.cpp
 * \brief  [GWF] expression editing stack (AQUIFER_GROUNDWATER_EXCHANGE_GUI_PLAN
 *         G4): engine-sourced vocabulary, completer proposals, engine-backed
 *         validation states (error column, empty = clear).
 */

#include "ui/widgets/gwfexpressionedit.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_model.h>
#include <openswmm/engine/openswmm_subcatchments.h>

#include <QCompleter>
#include <QObject>
#include <QSignalSpy>
#include <QTest>

using openswmmvis::ui::GwfExpressionEdit;

class TestGwfExpressionEdit : public QObject
{
    Q_OBJECT

private slots:
    void vocabularyComesFromEngine()
    {
        SWMM_Engine e = swmm_engine_new();
        QVERIFY(e);
        GwfExpressionEdit edit(e);

        QCOMPARE(edit.variableNames().size(), swmm_gwf_variable_count(e));
        QCOMPARE(edit.variableDescriptions().size(), edit.variableNames().size());
        QCOMPARE(edit.functionNames().size(), swmm_gwf_function_count(e));
        QVERIFY(edit.variableNames().contains(QStringLiteral("HGW")));
        QVERIFY(edit.variableNames().contains(QStringLiteral("HSW")));
        QVERIFY(edit.functionNames().contains(QStringLiteral("sqrt")));
        QVERIFY(edit.functionNames().contains(QStringLiteral("min")));
        // Descriptions are non-empty so the Insert-variable menu has text.
        for (const QString &d : edit.variableDescriptions())
            QVERIFY(!d.isEmpty());

        swmm_engine_destroy(e);
    }

    void completerProposesVariablesForPrefix()
    {
        SWMM_Engine e = swmm_engine_new();
        QVERIFY(e);
        GwfExpressionEdit edit(e);
        auto *completer = edit.findChild<QCompleter *>();
        QVERIFY(completer);

        // "HG" → HGW and HGS (both engine variables); matching is on the
        // bare-name role, so the "NAME — description" display text does
        // not get in the way.
        completer->setCompletionPrefix(QStringLiteral("HG"));
        QStringList proposals;
        for (int i = 0; i < completer->completionCount(); ++i) {
            completer->setCurrentRow(i);
            proposals << completer->currentCompletion();
        }
        QVERIFY2(proposals.contains(QStringLiteral("HGW")), qPrintable(proposals.join(',')));
        QVERIFY2(proposals.contains(QStringLiteral("HGS")), qPrintable(proposals.join(',')));
        QVERIFY(!proposals.contains(QStringLiteral("HSW")));

        // Functions complete case-insensitively too.
        completer->setCompletionPrefix(QStringLiteral("SQ"));
        QVERIFY(completer->completionCount() >= 1);
        completer->setCurrentRow(0);
        QCOMPARE(completer->currentCompletion(), QStringLiteral("sqrt"));

        swmm_engine_destroy(e);
    }

    void validationStates()
    {
        SWMM_Engine e = swmm_engine_new();
        QVERIFY(e);
        GwfExpressionEdit edit(e);
        QSignalSpy spy(&edit, &GwfExpressionEdit::validationChanged);

        edit.setExpression(QStringLiteral("0.001*(HGW-10)"));
        QVERIFY(spy.count() >= 1);
        auto last = spy.takeLast();
        QVERIFY(last.at(0).toBool());
        QVERIFY(edit.isValid());

        // Misspelled variable: rejected, column points at the identifier.
        spy.clear();
        edit.setExpression(QStringLiteral("2 * HGWW"));
        last = spy.takeLast();
        QVERIFY(!last.at(0).toBool());
        QVERIFY(!edit.isValid());
        QVERIFY(last.at(1).toString().contains(QStringLiteral("HGWW")));
        QCOMPARE(last.at(2).toInt(), 4);

        // Unbalanced parenthesis.
        spy.clear();
        edit.setExpression(QStringLiteral("log("));
        last = spy.takeLast();
        QVERIFY(!last.at(0).toBool());
        QVERIFY(!last.at(1).toString().isEmpty());

        // Empty is "clear the expression" — valid, no message (the engine
        // validator itself reports empty as invalid; the widget short-circuits).
        spy.clear();
        edit.setExpression(QString());
        last = spy.takeLast();
        QVERIFY(last.at(0).toBool());
        QVERIFY(last.at(1).toString().isEmpty());
        QVERIFY(edit.isValid());

        swmm_engine_destroy(e);
    }

    void insertAtCursorAndEnterCommit()
    {
        SWMM_Engine e = swmm_engine_new();
        QVERIFY(e);
        GwfExpressionEdit edit(e);
        edit.setExpression(QStringLiteral("2*"));
        edit.moveCursor(QTextCursor::End);
        edit.insertAtCursor(QStringLiteral("HGW"));
        QCOMPARE(edit.expression(), QStringLiteral("2*HGW"));

        // Enter commits (single-line) instead of inserting a newline.
        QSignalSpy done(&edit, &GwfExpressionEdit::editingFinished);
        QTest::keyClick(&edit, Qt::Key_Return);
        QCOMPARE(done.count(), 1);
        QVERIFY(!edit.expression().contains(QLatin1Char('\n')));

        swmm_engine_destroy(e);
    }
};

QTEST_MAIN(TestGwfExpressionEdit)
#include "test_gwfexpressionedit.moc"
