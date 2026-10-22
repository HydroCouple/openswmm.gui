/**
 * @file      gwfexpressionedit.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     [GWF] custom groundwater-flow expression editing stack: syntax
 *         highlighter + single-line completing editor with debounced
 *         engine validation — the groundwater peer of
 *         TreatmentExpressionEdit / TreatmentSyntaxHighlighter.
 *
 * Unlike the treatment stack, the vocabulary is NOT hard-coded: both the
 * highlighter and the completer read the variable / function lists from
 * the engine (swmm_gwf_variable_* / swmm_gwf_function_*) at construction,
 * so the GUI cannot drift from the grammar. A static fallback list is used
 * only when no engine handle is available (headless previews).
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_GWFEXPRESSIONEDIT_H
#define OPENSWMMVIS_UI_GWFEXPRESSIONEDIT_H

#include <QPalette>
#include <QSet>
#include <QStringList>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QTextEdit>

class QCompleter;

namespace openswmmvis::ui
{

/// Palette-driven highlighter for one [GWF] expression line.
class GwfSyntaxHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    /*! \param variables upper-case variable names (HGW, HSW, …)
     *  \param functions lower-case function names (sqrt, log, …)
     * @brief Attach palette-driven lexical formatting to an expression document.
     * @param[in] doc QTextDocument receiving this highlighter as a QObject child.
     * @note Vocabulary is copied and normalized internally: variables uppercase, functions lowercase.
     */
    GwfSyntaxHighlighter(QTextDocument *doc, const QStringList &variables,
                         const QStringList &functions);

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

    QSet<QString> m_vars;  ///< upper-cased
    QSet<QString> m_funcs; ///< lower-cased
    QPalette m_palette;
    QTextCharFormat m_fmtVariable;
    QTextCharFormat m_fmtFunction;
    QTextCharFormat m_fmtOperator;
    QTextCharFormat m_fmtNumber;
    QTextCharFormat m_fmtUnknown;
};

/*! Single-line [GWF] expression editor: highlighter + Ctrl+Space /
 *  2-char-prefix completion (name — description) + debounced engine
 *  validation via swmm_gwf_validate_expression on the injected handle. */
class GwfExpressionEdit : public QTextEdit
{
    Q_OBJECT

public:
    /**
     * @brief Create a plain-text expression editor with lexical highlighting, completion and 250 ms debounced validation.
     * @param[in] engineHandle Borrowed engine handle; null permits preview editing without authoritative validation. Caller keeps a non-null handle alive and serializes access on the GUI thread.
     * @param[in] parent Optional QWidget owner of the editor and its child helpers.
     * @note Enter/Return commits instead of inserting a newline; programmatic/pasted text can still contain multiple lines. This widget validates but never applies expressions to model objects.
     */
    explicit GwfExpressionEdit(void *engineHandle, QWidget *parent = nullptr);

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
    /**
     * @brief Clear the borrowed engine, stop pending debounce and disable editing before engine destruction.
     * @note Existing text/vocabulary remain. A direct later validateNow() treats the absent engine as skipped success.
     */
    void invalidateEngine();

    /*! Last verdict from validateNow() (true until the first run).
     * @return Cached latest verdict; initially true and also true when validation is skipped for empty text/null engine.
     */
    [[nodiscard]] bool isValid() const { return m_valid; }

    /*! Engine vocabulary as loaded at construction — variables in the
     *  engine's order, descriptions parallel to variableNames(). Used by
     *  the dialog's "Insert variable" menu.
     * @return Variable-name snapshot in engine enumeration order, or the compiled null-engine fallback list.
     */
    [[nodiscard]] QStringList variableNames() const { return m_varNames; }
    /**
     * @brief Read descriptions for successfully loaded engine variable names.
     * @return Parallel description snapshot; null-engine fallback has no descriptions.
     */
    [[nodiscard]] QStringList variableDescriptions() const { return m_varDescs; }
    /**
     * @brief Read the construction-time completion function names.
     * @return Function-name snapshot from the engine, or compiled null-engine fallbacks.
     */
    [[nodiscard]] QStringList functionNames() const { return m_funcNames; }

    /*! Insert `text` at the cursor (Insert-variable menu).
     * @param[in] text Literal draft text replacing the current selection; the cursor then receives focus and normal text-change debounce applies.
     */
    void insertAtCursor(const QString &text);

signals:
    /*! ok=true → msg empty; otherwise the engine diagnostic + 0-based
     *  column (-1 when not attributable). Empty text reports ok with an
     *  empty message (clearing removes the expression).
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
     * @brief Populate variable names/descriptions and functions from the injected engine at construction.
     * @note Null engine selects compiled fallbacks. Failed API rows are skipped, description failures become empty strings. Non-null engine enumeration appends to existing lists, so this is a construction-time helper rather than a refresh operation.
     */
    void loadVocabulary_();
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
    GwfSyntaxHighlighter *m_highlighter = nullptr;
    QCompleter *m_completer = nullptr;
    class QTimer *m_debounce = nullptr;
    bool m_valid = true;
    QStringList m_varNames;
    QStringList m_varDescs;
    QStringList m_funcNames;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_GWFEXPRESSIONEDIT_H
