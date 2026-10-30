/**
 * @file      titlenotespage.h
 * @brief     Simulation Options → Title / Notes.
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 *
 * Simulation Options → Title / Notes.
 *
 * A rich-text editor over the SWMM `[TITLE]` section. The engine's `[TITLE]`
 * is plain text, so the formatted document is persisted in the .oswp and the
 * engine gets the plain-text projection — the page prefers the .oswp copy on
 * read for exactly that reason.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef TITLENOTESPAGE_H
#define TITLENOTESPAGE_H

#include "ui/dialogs/simoptions/simoptionspage.h"

class QAction;
class QTextEdit;

namespace openswmmvis::ui
{

class TitleNotesPage : public SimOptionsPage
{
    Q_OBJECT

public:
    /**
     * @brief Build the rich-title draft editor and formatting actions.
     * @param ctx Borrowed GUI-thread context that must outlive this page and supply
     * valid engine/project handles while accessed.
     * @param parent Optional widget owner; null leaves lifetime to the caller.
     * Creates controls only; caller invokes read() to hydrate the initial baseline.
     */
    explicit TitleNotesPage(SimOptionsContext &ctx, QWidget *parent = nullptr);

    /**
     * @brief Return the localized sidebar caption.
     * @return Owned Title / Notes string.
     */
    [[nodiscard]] QString title() const override;
    /**
     * @brief Hydrate the editor and capture its normalized HTML baseline with editor signals
     * blocked.
     * Prefers nonempty project notesHtml; otherwise reads engine title lines through
     * 1024-byte buffers, skipping failed getters, or clears without either source.
     * Does not write an engine/project file or report failed/truncated reads separately.
     */
    void read() override;
    /**
     * @brief Project edited HTML into the engine plain title and project rich-note draft.
     * @return One only when title_clear then title_set both succeed, otherwise zero;
     * unchanged HTML/missing editor is an early no-op. Project HTML can still change
     * when the returned count is zero. Clear/set is not atomic: a failed set can
     * leave a cleared title. The HTML baseline advances after every changed attempt,
     * including failure or absent engine. Empty plain text clears project HTML;
     * nonempty plain text retains normalized HTML even without explicit formatting.
     * Does not save the input/project files or record a common option key.
     */
    int write() override;

private:
    /**
     * @brief Create owned formatting toolbar/editor and its page-context signal callbacks once.
     * Checked booleans drive bold/italic/underline current character format. A local
     * style callback takes a QTextListFormat style and creates a list at the borrowed
     * editor cursor; bullet/number actions choose disc/decimal. A borrowed character
     * format from cursor changes updates action checks. Tags the editor as [TITLE];
     * these callbacks change the draft, without direct engine writes.
     */
    void buildUi();

    QTextEdit *m_notesEdit = nullptr;
    QAction *m_boldAction = nullptr;
    QAction *m_italicAction = nullptr;
    QAction *m_underlineAction = nullptr;

    /*! Document as loaded, so write() can tell an edit from a re-render. */
    QString m_initialHtml;
};

} // namespace openswmmvis::ui

#endif // TITLENOTESPAGE_H
