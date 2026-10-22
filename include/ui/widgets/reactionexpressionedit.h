/**
 * @file      reactionexpressionedit.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Reaction-expression editing stack (G-B1): syntax highlighter,
 *         single-line completing editor, and the table-cell delegate — the
 *         [REACTION_*] peer of TreatmentExpressionEdit, cloned from it.
 *
 * Unlike the treatment stack's hard-coded lists, the vocabulary here is
 * LIVE: species/coefficients/terms come from the engine's discovery
 * getters (swmm_reaction_species_get and friends) and the hydraulic
 * variables / functions from the engine-less static getters
 * (swmm_reaction_hydvar_get / swmm_reaction_function_get) — the compiler's
 * own tables, so highlighting and completion cannot drift from what
 * actually compiles. The authoritative verdict stays with
 * swmm_reaction_validate_expression.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_REACTIONEXPRESSIONEDIT_H
#define OPENSWMMVIS_UI_REACTIONEXPRESSIONEDIT_H

#include <QPalette>
#include <QSet>
#include <QStringList>
#include <QStyledItemDelegate>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QTextEdit>

class QCompleter;

namespace openswmmvis::ui
{

/// Palette-driven highlighter for one reaction expression line. Identifier
/// classes are injected (rebuilt from the engine on demand).
class ReactionSyntaxHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    /**
     * @brief Attach palette-driven lexical formatting to an expression document.
     * @param[in] doc QTextDocument receiving this highlighter as a QObject child.
     */
    explicit ReactionSyntaxHighlighter(QTextDocument *doc);

    /// Static vocabulary from the compiler's own tables (engine-less).
    /**
     * @brief Discover hydraulic variable names from the engine-independent compiler vocabulary.
     * @return Successful getter results in API order; failed rows are omitted.
     */
    static QStringList hydVarNames();
    /**
     * @brief Discover function names from the engine-independent compiler vocabulary.
     * @return Successful getter results in API order; function arities are not retained.
     */
    static QStringList functionNames();

    /// Model vocabulary (species + coefficients + terms + pollutants),
    /// case-sensitive — the compiler matches them exactly.
    /**
     * @param[in] names Complete case-sensitive identifier set replacing the prior model vocabulary; immediately rehighlights.
     */
    void setModelIdentifiers(const QSet<QString> &names);

    /**
     * @brief Replace token colours/formats and rehighlight the attached document.
     * @param[in] palette Active colour roles for variables/functions/operators/numbers/unknown tokens; invalid colours use compiled fallbacks.
     */
    void setPalette(const QPalette &palette);

protected:
    /**
     * @brief Assign lexical formats to operators, number-like spans and identifier tokens in one text block.
     * @param[in] text Block text supplied by QSyntaxHighlighter; no document content is changed.
     * @note Lexical colouring is advisory; numeric syntax and identifier validity are decided by engine validation. Model identifiers are matched exactly before case-insensitive functions/hydraulic variables.
     */
    void highlightBlock(const QString &text) override;

private:
    /**
     * @brief Recreate cached text formats from the active palette roles.
     * @note Does not rehighlight on its own; setPalette() calls rehighlight afterward.
     */
    void rebuildFormats_();

    QPalette m_palette;
    QSet<QString> m_modelIdents;
    QTextCharFormat m_fmtVariable;   ///< hydraulic variables
    QTextCharFormat m_fmtModelIdent; ///< species/coeffs/terms/pollutants
    QTextCharFormat m_fmtFunction;
    QTextCharFormat m_fmtOperator;
    QTextCharFormat m_fmtNumber;
    QTextCharFormat m_fmtUnknown;
};

/*! Single-line reaction-expression editor: live-vocabulary highlighter +
 *  Ctrl+Space / 2-char-prefix completion + debounced engine validation via
 *  swmm_reaction_validate_expression on the injected handle + scope. */
class ReactionExpressionEdit : public QTextEdit
{
    Q_OBJECT

public:
    /*! \param scope SWMM_RXN_SCOPE_* the validator uses (default PIPE).
     * @brief Create a plain-text expression editor with lexical highlighting, completion and 250 ms debounced validation.
     * @param[in] engineHandle Borrowed engine handle; null permits preview editing without authoritative validation. Caller keeps a non-null handle alive and serializes access on the GUI thread.
     * @param[in] parent Optional QWidget owner of the editor and its child helpers.
     * @note Enter/Return commits instead of inserting a newline; programmatic/pasted text can still contain multiple lines. This widget validates but never applies expressions to model objects.
     */
    explicit ReactionExpressionEdit(void *engineHandle, int scope, QWidget *parent = nullptr);

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

    /*! Re-pull species/coefficients/terms/pollutants from the engine into
     *  the completer + highlighter (call after CRUD elsewhere).
     * @note Reads species, coefficients, terms and pollutants (skipping failed getters), then appends static functions/hydraulic variables and sorts completion labels case-insensitively. No engine state is changed.
     */
    void refreshVocabulary();

    /*! Run validation immediately (also runs debounced on every edit).
     * @note Trims the draft before calling the engine validator. Empty text or a null engine emits success with an empty diagnostic and column -1; success with no engine means validation was skipped. No model assignment occurs.
     */
    void validateNow();
    /**
     * @brief Clear the borrowed engine, stop pending debounce and disable editing before engine destruction.
     * @note Existing text/vocabulary remain. A direct later validateNow() treats the absent engine as skipped success.
     */
    void invalidateEngine();

signals:
    /**
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
    int m_scope = 1; // SWMM_RXN_SCOPE_PIPE
    ReactionSyntaxHighlighter *m_highlighter = nullptr;
    QCompleter *m_completer = nullptr;
    class QTimer *m_debounce = nullptr;
};

/*! Cell delegate hosting ReactionExpressionEdit; forwards live validation
 *  so the hosting page's banner can mirror it. The scope for each cell
 *  comes from the model's ScopeRole (falls back to the constructor's). */
class ReactionExpressionDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    /// Model data role carrying the cell's SWMM_RXN_SCOPE_* (int).
    static constexpr int ScopeRole = 0x0100 + 77;

    /**
     * @brief Create a delegate that hosts validating expression editors and forwards their verdicts.
     * @param[in] engineHandle Borrowed handle copied into newly created editors; caller must coordinate its lifetime.
     * @param[in] parent Optional QObject owner.
     * @param[in] defaultScope SWMM_RXN_SCOPE_* fallback when a cell exposes no ScopeRole.
     */
    explicit ReactionExpressionDelegate(void *engineHandle, int defaultScope,
                                        QObject *parent = nullptr);

    /**
     * @brief Clear the handle used for future delegate-created editors.
     * @note Does not invalidate any already created editors; their lifetime/engine invalidation must be handled separately.
     */
    void invalidateEngine() { m_engine = nullptr; }

    /**
     * @brief Create the expression editor and connect Enter commits/closes plus live validation forwarding.
     * @param[in] parent QWidget owner of the new editor.
     * @param[in] option Qt style options, currently unused.
     * @param[in] index Target cell supplying optional ScopeRole (converted to int), otherwise the configured default scope.
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
    int m_defaultScope = 1;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_REACTIONEXPRESSIONEDIT_H
