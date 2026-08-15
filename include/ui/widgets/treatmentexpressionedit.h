/*!
 * \file   treatmentexpressionedit.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Treatment-expression editing stack (iteration 4): syntax
 *         highlighter, single-line completing editor, and the table-cell
 *         delegate that hosts it — the [TREATMENT] peer of the control
 *         rules editor's RuleSyntaxHighlighter / RuleCodeEditor.
 *
 * VOCAB DRIFT GUARD: the variable/function lists mirror the engine
 * treatment grammar (openswmm.engine src/engine/quality/Treatment.cpp
 * var_map/func_map — variables C R DT HRT Q V D AREA, functions exp log
 * ln sqrt min max abs sgn step). The authoritative accept/reject verdict
 * comes from swmm_treatment_validate_expression, so highlighting can lag
 * the grammar without ever mis-validating — but keep the lists in sync
 * when the engine grows a variable.
 */
#ifndef OPENSWMMVIS_UI_TREATMENTEXPRESSIONEDIT_H
#define OPENSWMMVIS_UI_TREATMENTEXPRESSIONEDIT_H

#include <QPalette>
#include <QStringList>
#include <QStyledItemDelegate>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QTextEdit>

class QCompleter;

namespace openswmmvis::ui {

/// Palette-driven highlighter for one treatment expression line.
class TreatmentSyntaxHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    explicit TreatmentSyntaxHighlighter(QTextDocument *doc);

    static QStringList variableNames();   ///< C R DT HRT Q V D AREA
    static QStringList functionNames();   ///< exp log ln sqrt min max abs sgn step
    static QStringList allKeywords();     ///< variables + functions

    void setPalette(const QPalette &palette);

protected:
    void highlightBlock(const QString &text) override;

private:
    void rebuildFormats_();

    QPalette        m_palette;
    QTextCharFormat m_fmtVariable;
    QTextCharFormat m_fmtFunction;
    QTextCharFormat m_fmtOperator;
    QTextCharFormat m_fmtNumber;
    QTextCharFormat m_fmtUnknown;
};

/*! Single-line expression editor: highlighter + Ctrl+Space /
 *  2-char-prefix completion + debounced engine validation (via
 *  swmm_treatment_validate_expression on the injected engine handle). */
class TreatmentExpressionEdit : public QTextEdit
{
    Q_OBJECT

public:
    explicit TreatmentExpressionEdit(void *engineHandle,
                                     QWidget *parent = nullptr);

    [[nodiscard]] QString expression() const { return toPlainText(); }
    void setExpression(const QString &text);

    /*! Run validation immediately (also runs debounced on every edit). */
    void validateNow();

signals:
    /*! ok=true → msg empty; otherwise the engine diagnostic + 0-based
     *  column (-1 when not attributable). Empty text reports ok with an
     *  empty message (clearing a cell removes the expression). */
    void validationChanged(bool ok, const QString &msg, int col);
    void editingFinished();   ///< Enter/Return pressed (single-line commit)

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void focusInEvent(QFocusEvent *e) override;

private:
    void showCompleter_(const QString &prefix);
    void insertCompletion_(const QString &completion);
    [[nodiscard]] QString textUnderCursor_() const;

    void                       *m_engine     = nullptr;
    TreatmentSyntaxHighlighter *m_highlighter = nullptr;
    QCompleter                 *m_completer  = nullptr;
    class QTimer               *m_debounce   = nullptr;
};

/*! Cell delegate for the Treatment page's Expression column. Forwards the
 *  hosted editor's live validation through validationChanged so the page
 *  banner can mirror it. */
class TreatmentExpressionDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit TreatmentExpressionDelegate(void *engineHandle,
                                         QObject *parent = nullptr);

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override;
    void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override;

signals:
    void validationChanged(bool ok, const QString &msg, int col) const;

private:
    void *m_engine = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_TREATMENTEXPRESSIONEDIT_H
