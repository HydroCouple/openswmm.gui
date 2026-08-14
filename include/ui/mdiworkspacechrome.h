#ifndef MDIWORKSPACECHROME_H
#define MDIWORKSPACECHROME_H

/*!
 * \file mdiworkspacechrome.h
 *
 * Correct QMdiArea's stale initial backdrop. Extracted from SWMMVis so
 * tests/gui/test_mdi_tab_maximize.cpp can drive the real code against a
 * bare QMdiArea instead of restating the logic.
 */

class QMdiArea;

namespace openswmmvis::ui {

/*!
 * \brief Make \a area's backdrop track the theme.
 *
 * Backdrop: QMdiArea snapshots palette(QPalette::Dark) once in its
 * constructor and has no PaletteChange handling, so a theme installed
 * after construction — as ThemeManager is, SWMMVis being built first —
 * leaves the workspace painted in the pre-theme brush. welcomeWidget is a
 * plain QWidget with no autoFillBackground, so that stale brush is what
 * fills the whole welcome tab.
 *
 * Safe to call once per area; a null \a area is a no-op.
 */
void installMdiWorkspaceChrome(QMdiArea *area);

}   // namespace openswmmvis::ui

#endif // MDIWORKSPACECHROME_H
