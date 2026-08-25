/*!
 * \file   reactionexpressionedit.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Reaction-expression editing stack (G-B1): syntax highlighter,
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

namespace openswmmvis::ui {

/// Palette-driven highlighter for one reaction expression line. Identifier
/// classes are injected (rebuilt from the engine on demand).
class ReactionSyntaxHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    explicit ReactionSyntaxHighlighter(QTextDocument *doc);

    /// Static vocabulary from the compiler's own tables (engine-less).
    static QStringList hydVarNames();
    static QStringList functionNames();

    /// Model vocabulary (species + coefficients + terms + pollutants),
    /// case-sensitive — the compiler matches them exactly.
    void setModelIdentifiers(const QSet<QString> &names);

    void setPalette(const QPalette &palette);

protected:
    void highlightBlock(const QString &text) override;

private:
    void rebuildFormats_();

    QPalette        m_palette;
    QSet<QString>   m_modelIdents;
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
    /*! \param scope SWMM_RXN_SCOPE_* the validator uses (default PIPE). */
    explicit ReactionExpressionEdit(void *engineHandle, int scope,
                                    QWidget *parent = nullptr);

    [[nodiscard]] QString expression() const { return toPlainText(); }
    void setExpression(const QString &text);

    /*! Re-pull species/coefficients/terms/pollutants from the engine into
     *  the completer + highlighter (call after CRUD elsewhere). */
    void refreshVocabulary();

    /*! Run validation immediately (also runs debounced on every edit). */
    void validateNow();

signals:
    void validationChanged(bool ok, const QString &msg, int col);
    void editingFinished();   ///< Enter/Return pressed (single-line commit)

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void focusInEvent(QFocusEvent *e) override;

private:
    void showCompleter_(const QString &prefix);
    void insertCompletion_(const QString &completion);
    [[nodiscard]] QString textUnderCursor_() const;

    void                      *m_engine      = nullptr;
    int                        m_scope       = 1;   // SWMM_RXN_SCOPE_PIPE
    ReactionSyntaxHighlighter *m_highlighter = nullptr;
    QCompleter                *m_completer   = nullptr;
    class QTimer              *m_debounce    = nullptr;
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

    explicit ReactionExpressionDelegate(void *engineHandle, int defaultScope,
                                        QObject *parent = nullptr);

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &option,
                          const QModelIndex &index) const override;
    void setEditorData(QWidget *editor, const QModelIndex &index) const override;
    void setModelData(QWidget *editor, QAbstractItemModel *model,
                      const QModelIndex &index) const override;

signals:
    void validationChanged(bool ok, const QString &msg, int col) const;

private:
    void *m_engine       = nullptr;
    int   m_defaultScope = 1;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_REACTIONEXPRESSIONEDIT_H
