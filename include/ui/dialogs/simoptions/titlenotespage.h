/*!
 * \file   titlenotespage.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 *
 * Simulation Options → Title / Notes.
 *
 * A rich-text editor over the SWMM `[TITLE]` section. The engine's `[TITLE]`
 * is plain text, so the formatted document is persisted in the .oswp and the
 * engine gets the plain-text projection — the page prefers the .oswp copy on
 * read for exactly that reason.
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
    explicit TitleNotesPage(SimOptionsContext &ctx, QWidget *parent = nullptr);

    [[nodiscard]] QString title() const override;
    void read() override;
    int  write() override;

private:
    void buildUi();

    QTextEdit *m_notesEdit       = nullptr;
    QAction   *m_boldAction      = nullptr;
    QAction   *m_italicAction    = nullptr;
    QAction   *m_underlineAction = nullptr;

    /*! Document as loaded, so write() can tell an edit from a re-render. */
    QString    m_initialHtml;
};

} // namespace openswmmvis::ui

#endif // TITLENOTESPAGE_H
