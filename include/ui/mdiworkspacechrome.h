#ifndef MDIWORKSPACECHROME_H
#define MDIWORKSPACECHROME_H

/*!
 * \file mdiworkspacechrome.h
 *
 * Correct QMdiArea's stale initial backdrop, and make the welcome tab paint
 * one of its own. Extracted from SWMMVis so
 * tests/gui/test_mdi_tab_maximize.cpp can drive the real code against a
 * bare QMdiArea instead of restating the logic.
 */

class QMdiArea;
class QWidget;

namespace openswmmvis::ui {

/*!
 * \brief Make \a area's backdrop track the theme and give \a welcome an
 *        opaque background of its own.
 *
 * Backdrop: QMdiArea snapshots palette(QPalette::Dark) once in its
 * constructor and has no PaletteChange handling, so a theme installed
 * after construction — as ThemeManager is, SWMMVis being built first —
 * leaves the workspace painted in the pre-theme brush.
 *
 * Welcome: uic emits welcomeWidget as a bare QWidget, so it has no
 * autoFillBackground and paints nothing behind its own children. That is
 * why the backdrop showed through the whole welcome tab — and the backdrop
 * is not the only thing below it. In TabbedView QMdiArea never *hides* the
 * outgoing sub-window; _q_deactivateAllWindows merely showNormal()s it
 * (qmdiarea.cpp:685) and relies on the incoming maximized window to cover
 * the viewport. So after welcome → model → welcome the model sub-window is
 * still sitting in the viewport as a 200x150 framed child, and a
 * transparent welcome on top of it is not a cover: the user sees a
 * detached, undocked model window painted over the welcome screen.
 *
 * setAutoFillBackground() fills with QPalette::Window, which ThemeManager
 * sets to surfaceWindow (thememanager.cpp:87) — the same token the backdrop
 * uses, so the welcome looks unchanged — and it is re-read at every paint,
 * so the Appearance switch needs no extra hook here.
 *
 * Safe to call once per area; a null \a area or \a welcome is a no-op.
 */
void installMdiWorkspaceChrome(QMdiArea *area, QWidget *welcome = nullptr);

}   // namespace openswmmvis::ui

#endif // MDIWORKSPACECHROME_H
