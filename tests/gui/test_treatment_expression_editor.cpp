/*!
 * \file   test_treatment_expression_editor.cpp
 * \brief  Iteration 4 — the treatment-expression editing stack: highlighter
 *         vocabulary (drift guard vs the engine grammar), completer wiring,
 *         live engine-backed validation states, and the cell delegate.
 */

#include "ui/widgets/treatmentexpressionedit.h"

#include <openswmm/engine/openswmm_engine.h>
#include <openswmm/engine/openswmm_quality.h>

#include <QCompleter>
#include <QObject>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QTest>

using openswmmvis::ui::TreatmentExpressionDelegate;
using openswmmvis::ui::TreatmentExpressionEdit;
using openswmmvis::ui::TreatmentSyntaxHighlighter;

class TestTreatmentExpressionEditor : public QObject
{
    Q_OBJECT

private slots:
    void vocabularyMatchesEngineGrammar()
    {
        // Drift guard: every vocab entry must validate as a bare operand
        // inside "C = <token>(1)" / "C = <token>" through the ENGINE's
        // validator, so the highlighter can never advertise an identifier
        // the grammar rejects.
        SWMM_Engine e = swmm_engine_new();
        QVERIFY(e);
        char err[256];
        int col = -1;
        for (const QString &v : TreatmentSyntaxHighlighter::variableNames()) {
            const QString expr = QStringLiteral("C = %1").arg(v);
            QCOMPARE(swmm_treatment_validate_expression(
                         e, expr.toUtf8().constData(), err, sizeof(err), &col),
                     SWMM_OK);
        }
        for (const QString &f : TreatmentSyntaxHighlighter::functionNames()) {
            const QString expr =
                (f == QLatin1String("min") || f == QLatin1String("max"))
                    ? QStringLiteral("C = %1(1, 2)").arg(f)
                    : QStringLiteral("C = %1(1)").arg(f);
            QCOMPARE(swmm_treatment_validate_expression(
                         e, expr.toUtf8().constData(), err, sizeof(err), &col),
                     SWMM_OK);
        }
        swmm_engine_destroy(e);
    }

    void editorHasHighlighterAndCompleter()
    {
        SWMM_Engine e = swmm_engine_new();
        TreatmentExpressionEdit edit(e);
        auto *completer = edit.findChild<QCompleter *>();
        QVERIFY(completer);
        // Completion vocabulary covers variables + functions.
        QStringList items;
        for (int i = 0; i < completer->model()->rowCount(); ++i)
            items << completer->model()
                         ->index(i, 0).data().toString();
        QVERIFY(items.contains(QStringLiteral("HRT")));
        QVERIFY(items.contains(QStringLiteral("exp")));
        QVERIFY(items.contains(QStringLiteral("AREA")));
        swmm_engine_destroy(e);
    }

    void validationStates()
    {
        SWMM_Engine e = swmm_engine_new();
        TreatmentExpressionEdit edit(e);
        QSignalSpy spy(&edit, &TreatmentExpressionEdit::validationChanged);

        edit.setExpression(QStringLiteral("R = 1.0 - exp(-0.5 * HRT)"));
        QVERIFY(spy.count() >= 1);
        auto last = spy.takeLast();
        QVERIFY(last.at(0).toBool());

        spy.clear();
        edit.setExpression(QStringLiteral("C = FLOW * 2"));
        last = spy.takeLast();
        QVERIFY(!last.at(0).toBool());
        QVERIFY(last.at(1).toString().contains(QStringLiteral("FLOW")));
        QCOMPARE(last.at(2).toInt(), 4);

        spy.clear();
        edit.setExpression(QStringLiteral("X = 1"));
        last = spy.takeLast();
        QVERIFY(!last.at(0).toBool());
        QVERIFY(!last.at(1).toString().isEmpty());

        // Empty is "clear the expression" — valid, no message.
        spy.clear();
        edit.setExpression(QString());
        last = spy.takeLast();
        QVERIFY(last.at(0).toBool());
        QVERIFY(last.at(1).toString().isEmpty());

        swmm_engine_destroy(e);
    }

    void delegateHostsExpressionEditor()
    {
        SWMM_Engine e = swmm_engine_new();
        TreatmentExpressionDelegate delegate(e);
        QStandardItemModel model(1, 2);
        model.setData(model.index(0, 1), QStringLiteral("R = 0.5"),
                      Qt::EditRole);

        QWidget parent;
        QWidget *editor = delegate.createEditor(&parent, {}, model.index(0, 1));
        auto *expr = qobject_cast<TreatmentExpressionEdit *>(editor);
        QVERIFY(expr);

        delegate.setEditorData(editor, model.index(0, 1));
        QCOMPARE(expr->expression(), QStringLiteral("R = 0.5"));

        expr->setExpression(QStringLiteral("C = min(C, 10)"));
        delegate.setModelData(editor, &model, model.index(0, 1));
        QCOMPARE(model.data(model.index(0, 1), Qt::EditRole).toString(),
                 QStringLiteral("C = min(C, 10)"));

        // Enter commits (single-line) instead of inserting a newline.
        QSignalSpy done(expr, &TreatmentExpressionEdit::editingFinished);
        QTest::keyClick(expr, Qt::Key_Return);
        QCOMPARE(done.count(), 1);
        QVERIFY(!expr->expression().contains(QLatin1Char('\n')));

        swmm_engine_destroy(e);
    }
};

QTEST_MAIN(TestTreatmentExpressionEditor)
#include "test_treatment_expression_editor.moc"
