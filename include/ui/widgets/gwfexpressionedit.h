/*!
 * \file   gwfexpressionedit.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  [GWF] custom groundwater-flow expression editing stack: syntax
 *         highlighter + single-line completing editor with debounced
 *         engine validation — the groundwater peer of
 *         TreatmentExpressionEdit / TreatmentSyntaxHighlighter.
 *
 * Unlike the treatment stack, the vocabulary is NOT hard-coded: both the
 * highlighter and the completer read the variable / function lists from
 * the engine (swmm_gwf_variable_* / swmm_gwf_function_*) at construction,
 * so the GUI cannot drift from the grammar. A static fallback list is used
 * only when no engine handle is available (headless previews).
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

namespace openswmmvis::ui {

/// Palette-driven highlighter for one [GWF] expression line.
class GwfSyntaxHighlighter : public QSyntaxHighlighter
{
    Q_OBJECT

public:
    /*! \param variables upper-case variable names (HGW, HSW, …)
     *  \param functions lower-case function names (sqrt, log, …) */
    GwfSyntaxHighlighter(QTextDocument *doc, const QStringList &variables,
                         const QStringList &functions);

    void setPalette(const QPalette &palette);

protected:
    void highlightBlock(const QString &text) override;

private:
    void rebuildFormats_();

    QSet<QString>   m_vars;    ///< upper-cased
    QSet<QString>   m_funcs;   ///< lower-cased
    QPalette        m_palette;
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
    explicit GwfExpressionEdit(void *engineHandle, QWidget *parent = nullptr);

    [[nodiscard]] QString expression() const { return toPlainText(); }
    void setExpression(const QString &text);

    /*! Run validation immediately (also runs debounced on every edit). */
    void validateNow();

    /*! Last verdict from validateNow() (true until the first run). */
    [[nodiscard]] bool isValid() const { return m_valid; }

    /*! Engine vocabulary as loaded at construction — variables in the
     *  engine's order, descriptions parallel to variableNames(). Used by
     *  the dialog's "Insert variable" menu. */
    [[nodiscard]] QStringList variableNames() const { return m_varNames; }
    [[nodiscard]] QStringList variableDescriptions() const { return m_varDescs; }
    [[nodiscard]] QStringList functionNames() const { return m_funcNames; }

    /*! Insert `text` at the cursor (Insert-variable menu). */
    void insertAtCursor(const QString &text);

signals:
    /*! ok=true → msg empty; otherwise the engine diagnostic + 0-based
     *  column (-1 when not attributable). Empty text reports ok with an
     *  empty message (clearing removes the expression). */
    void validationChanged(bool ok, const QString &msg, int col);
    void editingFinished();   ///< Enter/Return pressed (single-line commit)

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void focusInEvent(QFocusEvent *e) override;

private:
    void loadVocabulary_();
    void showCompleter_(const QString &prefix);
    void insertCompletion_(const QString &completion);
    [[nodiscard]] QString textUnderCursor_() const;

    void                 *m_engine      = nullptr;
    GwfSyntaxHighlighter *m_highlighter = nullptr;
    QCompleter           *m_completer   = nullptr;
    class QTimer         *m_debounce    = nullptr;
    bool                  m_valid       = true;
    QStringList           m_varNames;
    QStringList           m_varDescs;
    QStringList           m_funcNames;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_GWFEXPRESSIONEDIT_H
