/*!
 * \file   rulecodeeditor.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Slice BR Phase 6.8.2 — rich-text code editor for SWMM control rules.
 *
 * `QTextEdit` subclass (rich text, per user spec — **not** `QPlainTextEdit`).
 * Wires in:
 *   - `RuleSyntaxHighlighter` attached to the document.
 *   - `QCompleter` whose source model is rebuilt on each popup from the
 *     bound `SWMMModelLayer`'s live node / link / subcatchment names plus
 *     the static keyword vocabulary from `RuleSyntaxHighlighter::allKeywords()`.
 *   - `Ctrl+Space` force-trigger; otherwise the popup is auto-triggered
 *     after two characters typed at any position with completions.
 *
 * Save semantics live in `RulesEditorDialog`, not here — the editor is a
 * pure view widget that emits `textChanged` (inherited) which the dialog
 * picks up to (a) schedule validation via `RuleValidator::validateDebounced`
 * and (b) flush via `applyControlRuleReplace` on focus-out or list-selection
 * change.
 */
#ifndef OPENSWMMVIS_UI_WIDGETS_RULECODEEDITOR_H
#define OPENSWMMVIS_UI_WIDGETS_RULECODEEDITOR_H

#include <QPointer>
#include <QTextEdit>

class QCompleter;
class QStringListModel;
class SWMMModelLayer;

namespace openswmmvis::controls { class RuleSyntaxHighlighter; }

namespace openswmmvis::ui {

class RuleCodeEditor : public QTextEdit
{
    Q_OBJECT

public:
    explicit RuleCodeEditor(QWidget *parent = nullptr);
    ~RuleCodeEditor() override;

    /*! \brief Bind a model layer so the completer can resolve live node /
     *  link / subcatchment names. Re-bindable across project switches. */
    void setModelLayer(SWMMModelLayer *layer);

    openswmmvis::controls::RuleSyntaxHighlighter *highlighter() const noexcept
    { return m_highlighter; }

    QCompleter *completer() const noexcept { return m_completer; }

protected:
    void keyPressEvent(QKeyEvent *e) override;
    void focusInEvent(QFocusEvent *e) override;

private slots:
    void insertCompletion_(const QString &completion);

private:
    /*! \brief The token immediately under / left of the cursor (used as
     *  the completion prefix). Returns an empty string when the cursor
     *  is on whitespace. */
    QString textUnderCursor_() const;

    /*! \brief Rebuild the completer's source list. Called lazily on
     *  every popup so newly-added model objects show up without
     *  refresh. */
    void rebuildCompletions_();

    /*! \brief Show the popup at the current cursor position. */
    void showCompleter_(const QString &prefix);

    QPointer<SWMMModelLayer>                          m_layer;
    openswmmvis::controls::RuleSyntaxHighlighter     *m_highlighter   = nullptr;
    QCompleter                                        *m_completer    = nullptr;
    QStringListModel                                  *m_complModel   = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_RULECODEEDITOR_H
