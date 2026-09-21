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

#include <QString>

#include <functional>

class QMdiArea;
class QMdiSubWindow;
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

/*!
 * \brief Give every tab in \a area a tooltip naming its document in full.
 *
 * A tab is labelled with the model's BASE NAME, so two models of the same
 * name opened from different folders are indistinguishable on the tab bar.
 * \a pathFor supplies the full path for a sub-window; an empty return (an
 * untitled project, the welcome tab) falls back to the tab's own title, so
 * the tooltip is never present-but-blank.
 *
 * Two QMdiArea facts this relies on, both pinned by
 * tests/gui/test_mdi_tab_tooltips.cpp:
 *
 *   * QMdiArea never sets a tab tooltip itself. Its event filter handles
 *     WindowTitleChange and ModifiedChange by calling setTabText, and
 *     WindowIconChange by calling setTabIcon (qmdiarea.cpp:2646-2654) —
 *     nothing touches the tooltip, so what we set here is not overwritten
 *     when a title changes or a document goes dirty.
 *   * Tab index == position in subWindowList(), the same rule
 *     setSubWindowTabVisible() depends on. Closing a tab shifts every later
 *     index, so callers must re-run this after a sub-window is removed.
 *
 * A null \a area, or an area with no tab bar, is a no-op.
 */
void refreshSubWindowTabToolTips(
    QMdiArea *area,
    const std::function<QString(QMdiSubWindow *)> &pathFor);

}   // namespace openswmmvis::ui

#endif // MDIWORKSPACECHROME_H
