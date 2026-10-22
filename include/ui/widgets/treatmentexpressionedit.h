/**
 * @file      treatmentexpressionedit.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Treatment-expression editing stack (iteration 4): syntax
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
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

namespace openswmmvis::ui
{

/// Palette-driven highlighter for one treatment expression line.
class TreatmentSyntaxHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    /**
     * @brief Attach palette-driven lexical formatting to an expression document.
     * @param[in] doc QTextDocument receiving this highlighter as a QObject child.
     */
    explicit TreatmentSyntaxHighlighter(QTextDocument *doc);

    /**
     * @brief Provide the compiled treatment lexical/completion vocabulary.
     * @return C, R, DT, HRT, Q, V, D and AREA.
     */
    static QStringList variableNames(); ///< C R DT HRT Q V D AREA
    /**
     * @brief Provide the compiled treatment lexical/completion vocabulary.
     * @return exp, log, ln, sqrt, min, max, abs, sgn and step.
     */
    static QStringList functionNames(); ///< exp log ln sqrt min max abs sgn step
    /**
     * @brief Provide the compiled treatment lexical/completion vocabulary.
     * @return Variable names followed by function names in the compiled completion order.
     */
    static QStringList allKeywords(); ///< variables + functions

    /**
     * @brief Replace token colours/formats and rehighlight the attached document.
     * @param[in] palette Active colour roles for variables/functions/operators/numbers/unknown tokens; invalid colours use compiled fallbacks.
     */
    void setPalette(const QPalette &palette);

protected:
    /**
     * @brief Assign lexical formats to operators, number-like spans and identifier tokens in one text block.
     * @param[in] text Block text supplied by QSyntaxHighlighter; no document content is changed.
     * @note Lexical colouring is advisory; numeric syntax and identifier validity are decided by engine validation. Vocabulary identifiers match case-insensitively.
     */
    void highlightBlock(const QString &text) override;

private:
    /**
     * @brief Recreate cached text formats from the active palette roles.
     * @note Does not rehighlight on its own; setPalette() calls rehighlight afterward.
     */
    void rebuildFormats_();

    QPalette m_palette;
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
    /**
     * @brief Create a plain-text expression editor with lexical highlighting, completion and 250 ms debounced validation.
     * @param[in] engineHandle Borrowed engine handle; null permits preview editing without authoritative validation. Caller keeps a non-null handle alive and serializes access on the GUI thread.
     * @param[in] parent Optional QWidget owner of the editor and its child helpers.
     * @note Enter/Return commits instead of inserting a newline; programmatic/pasted text can still contain multiple lines. This widget validates but never applies expressions to model objects.
     */
    explicit TreatmentExpressionEdit(void *engineHandle, QWidget *parent = nullptr);

    /**
     * @brief Read the complete current plain-text draft.
     * @return Text as entered, without trimming or validation.
     */
    [[nodiscard]] QString expression() const { return toPlainText(); }
    /**
     * @brief Replace document text and immediately publish a validation verdict.
     * @param[in] text Draft text, including empty or multiline content. QTextEdit textChanged may also schedule a later debounced validation.
     */
    void setExpression(const QString &text);

    /*! Run validation immediately (also runs debounced on every edit).
     * @note Trims the draft before calling the engine validator. Empty text or a null engine emits success with an empty diagnostic and column -1; success with no engine means validation was skipped. No model assignment occurs.
     */
    void validateNow();

signals:
    /*! ok=true → msg empty; otherwise the engine diagnostic + 0-based
     *  column (-1 when not attributable). Empty text reports ok with an
     *  empty message (clearing a cell removes the expression).
     * @brief Publish the most recent immediate/debounced validation result.
     * @param[in] ok True for engine acceptance or skipped validation (empty text/null handle).
     * @param[in] msg Engine diagnostic text, normally empty on success.
     * @param[in] col Zero-based engine-reported error column, or -1 when not attributable.
     */
    void validationChanged(bool ok, const QString &msg, int col);
    void editingFinished(); ///< Enter/Return pressed (single-line commit)

protected:
    /**
     * @brief Route popup keys to the completer, commit on Enter/Return, and trigger completion on Ctrl+Space or a two-character word prefix.
     * @param[in] e Required Qt key event; ordinary input is forwarded to QTextEdit, and nonword/short prefixes hide the popup.
     */
    void keyPressEvent(QKeyEvent *e) override;
    /**
     * @brief Rebind completion to this editor before forwarding focus handling.
     * @param[in] e Required Qt focus event forwarded to QTextEdit.
     */
    void focusInEvent(QFocusEvent *e) override;

private:
    /**
     * @brief Show the current completion vocabulary at the text cursor.
     * @param[in] prefix Prefix matched case-insensitively by the configured completer; empty allows all matches.
     */
    void showCompleter_(const QString &prefix);
    /**
     * @brief Replace WordUnderCursor with an accepted bare completion token.
     * @param[in] completion Selected vocabulary token; inserted only when this editor is the completer's current widget.
     */
    void insertCompletion_(const QString &completion);
    /**
     * @brief Obtain Qt's WordUnderCursor selection for completion.
     * @return Selected token text, including empty at positions without a word; document text is unchanged.
     */
    [[nodiscard]] QString textUnderCursor_() const;

    void *m_engine = nullptr;
    TreatmentSyntaxHighlighter *m_highlighter = nullptr;
    QCompleter *m_completer = nullptr;
    class QTimer *m_debounce = nullptr;
};

/*! Cell delegate for the Treatment page's Expression column. Forwards the
 *  hosted editor's live validation through validationChanged so the page
 *  banner can mirror it. */
class TreatmentExpressionDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    /**
     * @brief Create a delegate that hosts validating expression editors and forwards their verdicts.
     * @param[in] engineHandle Borrowed handle copied into newly created editors; caller must coordinate its lifetime.
     * @param[in] parent Optional QObject owner.
     */
    explicit TreatmentExpressionDelegate(void *engineHandle, QObject *parent = nullptr);

    /**
     * @brief Create the expression editor and connect Enter commits/closes plus live validation forwarding.
     * @param[in] parent QWidget owner of the new editor.
     * @param[in] option Qt style options, currently unused.
     * @param[in] index Target cell, currently unused during creation.
     * @return Newly allocated parent-owned editor; validation does not gate commitData()/closeEditor().
     */
    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override;
    /**
     * @brief Load EditRole draft text into a hosted editor and validate immediately.
     * @param[in,out] editor Hosted expression editor; other widget types delegate to Qt.
     * @param[in] index Source cell; its EditRole is converted to QString.
     */
    void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    /**
     * @brief Submit trimmed draft text through model EditRole, or use Qt for other editor widgets.
     * @param[in] editor Hosted expression editor.
     * @param[in,out] model Required model receiving setData(); rejection is not independently reported.
     * @param[in] index Target cell; no validation verdict is checked before submission.
     */
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override;

signals:
    /**
     * @brief Forward a hosted editor's live validation without filtering or storing it.
     * @param[in] ok Acceptance or skipped-validation status.
     * @param[in] msg Engine diagnostic text.
     * @param[in] col Zero-based error column or -1.
     */
    void validationChanged(bool ok, const QString &msg, int col) const;

private:
    void *m_engine = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_TREATMENTEXPRESSIONEDIT_H
