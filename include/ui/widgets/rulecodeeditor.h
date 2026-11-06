/**
 * @file      rulecodeeditor.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Slice BR Phase 6.8.2 — rich-text code editor for SWMM control rules.
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
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_WIDGETS_RULECODEEDITOR_H
#define OPENSWMMVIS_UI_WIDGETS_RULECODEEDITOR_H

#include <QPointer>
#include <QTextEdit>

class QCompleter;
class QStringListModel;
class SWMMModelLayer;

namespace openswmmvis::controls
{
class RuleSyntaxHighlighter;
}

namespace openswmmvis::ui
{

class RuleCodeEditor : public QTextEdit
{
    Q_OBJECT

public:
    /**
     * @brief Create a monospace, no-wrap control-rule editor with lexical highlighting and completion.
     * @param[in] parent Optional QWidget owner; content accepts plain text and engine mutations remain the host dialog's responsibility.
     */
    explicit RuleCodeEditor(QWidget *parent = nullptr);
    /**
     * @brief Destroy the widget and its QObject-owned highlighter/completion helpers.
     */
    ~RuleCodeEditor() override;

    /*! \brief Bind a model layer so the completer can resolve live node /
     *  link / subcatchment names. Re-bindable across project switches.
     * @param[in] layer Borrowed layer tracked by QPointer; null leaves static keywords only. Immediately rebuilds completion entries.
     */
    void setModelLayer(SWMMModelLayer *layer);

    /**
     * @brief Access the document-owned lexical highlighter for palette configuration.
     * @return Borrowed highlighter pointer; lifetime follows the editor document.
     */
    openswmmvis::controls::RuleSyntaxHighlighter *highlighter() const noexcept
    {
        return m_highlighter;
    }

    /**
     * @brief Access the owned completion controller for integration.
     * @return Borrowed child pointer; do not delete independently.
     */
    QCompleter *completer() const noexcept { return m_completer; }

protected:
    /**
     * @brief Route active-popup navigation, force completion on Ctrl+Space, and auto-complete word prefixes of at least two characters.
     * @param[in] e Required Qt key event; ordinary input/newlines forward to QTextEdit. This editor does not itself validate or save the draft.
     */
    void keyPressEvent(QKeyEvent *e) override;
    /**
     * @brief Rebind the completer widget and forward focus processing.
     * @param[in] e Required Qt focus event.
     */
    void focusInEvent(QFocusEvent *e) override;

private slots:
    /**
     * @brief Insert the selected completion suffix at the current word end.
     * @param[in] completion Accepted full token; the completer prefix length determines the appended suffix. No-op when this is not the active completion widget.
     */
    void insertCompletion_(const QString &completion);

private:
    /*! \brief The token immediately under / left of the cursor (used as
     *  the completion prefix). Returns an empty string when the cursor
     *  is on whitespace. */
    QString textUnderCursor_() const;

    /*! \brief Rebuild the completer's source list. Called lazily on
     *  every popup so newly-added model objects show up without
     *  refresh.
     * @note Collects static keywords and live spatial-object identities, including rain gages, from the bound layer. Removes exact duplicate strings without context filtering; a destroyed layer is safely omitted.
     */
    void rebuildCompletions_();

    /*! \brief Show the popup at the current cursor position.
     * @param[in] prefix Case-insensitive token prefix; empty offers all current entries. Vocabulary is rebuilt and the first match preselected before the popup opens.
     */
    void showCompleter_(const QString &prefix);

    QPointer<SWMMModelLayer> m_layer;
    openswmmvis::controls::RuleSyntaxHighlighter *m_highlighter = nullptr;
    QCompleter *m_completer = nullptr;
    QStringListModel *m_complModel = nullptr;
};

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_WIDGETS_RULECODEEDITOR_H
