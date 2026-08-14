#ifndef MDIWORKSPACECHROME_H
#define MDIWORKSPACECHROME_H

/*!
 * \file mdiworkspacechrome.h
 *
 * Two corrections to QMdiArea's out-of-the-box behaviour that the app's
 * TabbedView + hide-in-place welcome tab both depend on. Extracted from
 * SWMMVis so tests/gui/test_mdi_tab_maximize.cpp can drive the real code
 * against a bare QMdiArea instead of restating the logic.
 */

class QMdiArea;

namespace openswmmvis::ui {

/*!
 * \brief Make \a area's backdrop track the theme and keep its activated
 *        sub-window maximized in TabbedView.
 *
 * Backdrop: QMdiArea snapshots palette(QPalette::Dark) once in its
 * constructor and has no PaletteChange handling, so a theme installed
 * after construction — as ThemeManager is, SWMMVis being built first —
 * leaves the workspace painted in the pre-theme brush. welcomeWidget is a
 * plain QWidget with no autoFillBackground, so that stale brush is what
 * fills the whole welcome tab.
 *
 * Maximize: in TabbedView QMdiArea never *hides* an inactive sub-window;
 * it relies on the active one being maximized to cover the viewport. That
 * state is handed over in QMdiAreaPrivate::_q_deactivateAllWindows only
 * when an outgoing window is BOTH maximized and visible, so hiding a
 * sub-window in place (the welcome tab's close path) breaks the chain and
 * the next activation lands in Normal state — a small framed child, Qt
 * default window icon and all, painted over the tab the user selected.
 *
 * Explicitly hidden sub-windows are left alone so re-asserting the
 * maximized state cannot resurrect a welcome tab the user closed.
 * Safe to call once per area; a null \a area is a no-op.
 */
void installMdiWorkspaceChrome(QMdiArea *area);

}   // namespace openswmmvis::ui

#endif // MDIWORKSPACECHROME_H
