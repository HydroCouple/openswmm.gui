/**
 * @file      dialoglayoutpersistence.h
 * @author    Caleb Buahin <caleb.buahin@gmail.com>
 * @date      2026
 * @license   GPL-3.0-or-later
 * @brief     Step E.1 + Step H — free helpers for dialog layout persistence
 *         via QSettings and a uniform always-on-top policy.
 *
 * Iteration 2 (D1): persistence is applied automatically by the app-wide
 * DialogLayoutWatcher (restore on first Show — AFTER the ctor, so hard-
 * coded ctor resize()/defaults become the first-run values; save on
 * Hide/Close, which covers QDialog::done()/exec paths). Wiring a dialog
 * is therefore just NAMING it and the state worth keeping:
 *
 *   setObjectName(QStringLiteral("MyDialog"));           // enables persistence
 *   m_splitter->setObjectName(QStringLiteral("main"));   // + splitter sizes
 *   m_table->setObjectName(QStringLiteral("grid"));      // + header state
 *   m_tabs->setObjectName(QStringLiteral("tabs"));       // + current tab
 *   m_pages->setObjectName(QStringLiteral("pages"));     // + nav page (user nav
 *                                                        //   ONLY — never stacks
 *                                                        //   that follow data)
 *   viewAction->setObjectName(QStringLiteral("showX"));  // + checkable view toggle
 *
 * Set the "noLayoutPersistence" dynamic property to opt a dialog out.
 * Children named "qt_*" (Qt-internal) are ignored. Multi-instance dialogs
 * share their class key — last close wins.
 * Single-column trees with hidden headers keep their authored sizing rather
 * than persisting a column width the user cannot resize.
 *
 * QSettings keys live under `Dialogs/<objectName>/...` so they're isolated
 * from main-window state and from one another.
 * @copyright Copyright (c) 2026 Caleb Buahin. All rights reserved.
 */

#ifndef OPENSWMMVIS_UI_DIALOGS_DIALOGLAYOUTPERSISTENCE_H
#define OPENSWMMVIS_UI_DIALOGS_DIALOGLAYOUTPERSISTENCE_H

#include <QRect>
#include <QtGlobal>            // Q_OS_MACOS
#include <QtCore/qnamespace.h> // Qt::WindowFlags, Qt::Tool, Qt::WindowStaysOnTopHint

class QDialog;
class QScreen;
class QWidget;

namespace openswmmvis::ui
{

/*! \brief Persist the dialog's geometry + every named QSplitter under it.
 *
 *  Walks `root->findChildren<QSplitter*>()` and stores each splitter's
 *  `saveState()` under `Dialogs/<root-objectName>/splitter/<splitter-objectName>`.
 *  Splitters without an `objectName` are skipped (forces deliberate opt-in).
 *
 *  No-op when \p root has no objectName (we'd have nowhere to file it).
 *  With windowLayout=true (default), geometry is saved for the containing
 *  window. Pass false for embedded plot content: use the root objectName
 *  and persist only child layout state, never the containing window geometry.
 * @param[in] root Optional borrowed widget root; resolve key from containing window when
 *   windowLayout, root itself otherwise. Null/empty resolved name no-op.
 * @param[in] windowLayout True saves containing-window CLIENT QRect; false child-only layout state,
 *   same traversal rooted at provided widget.
 * @note Write QSettings Dialogs/key geometry plus recursively found named non-qt_
 *   splitters/table/tree headers/tabs/stacks/checkable QAction toggles. Only QAction has direct
 *   noLayoutPersistence guard here; root opt-out enforced by watcher, not free helper. Named root
 *   child itself not included by findChildren. Duplicate child names collide, last traversed state
 *   wins; stores full header state, not just widths, except for single-column trees with hidden
 *   headers. No settings error checking, forced sync or application-content/engine write.
 *   GUI thread, setters not invoked during save.
 */
void saveDialogLayout(QWidget *root, bool windowLayout = true);

/*! \brief Inverse of saveDialogLayout — restore geometry + splitter states.
 *
 *  Restored geometry is clamped to currently-available screen real estate
 *  so a saved position on a now-disconnected monitor doesn't render
 *  off-screen.
 *
 *  \returns true if any state was restored (caller can skip its defaults);
 *           false on first-open (caller applies its hard-coded defaults).
 * @param[in] root Optional borrowed root, key/window resolution as save; null/empty resolved key
 *   false with no changes.
 * @param[in] windowLayout True restore saved window rectangle when valid; false skip geometry but
 *   restore children.
 * @return True if valid geometry, successful splitter/header restore, valid tab/page index or saved
 *   action toggle processed; false can STILL alter every descendant table/tree header mode to
 *   Interactive/no stretch, except single-column trees with hidden headers. Does not prove every
 *   state key restored.
 * @note Restore QSettings children under same naming rules, geometry clamped, out-of-range tab/page
 *   ignored. Signals are NOT blocked: changing tab/page/action emits normal behavior/side effects.
 *   At end force table/tree descendant headers (even unnamed) Interactive and
 *   stretchLastSection=false, overriding restored resize mode. Single-column trees with hidden
 *   headers retain their authored sizing and ignore stale saved header state. Root opt-out not
 *   checked here. No content/engine save, transaction or rollback on partial failure.
 */
bool restoreDialogLayout(QWidget *root, bool windowLayout = true);

/*! \brief Clamp a saved top-level geometry into currently-available screen
 *  space, so a window can never be restored somewhere unreachable.
 *
 *  \p saved is a CLIENT rect (what QWidget::geometry() / setGeometry() use).
 *  \p titleBarStrip is the height of the frame decoration above that rect —
 *  the window's only drag handle, which must stay inside the available area
 *  or the window cannot be moved at all. Pass -1 for a platform default.
 *
 *  Rules, in order:
 *   - target screen = the one the window's FRAME overlaps most (a rect
 *     straddling two monitors keeps the monitor it mostly sits on, instead
 *     of being yanked to primary because its center fell in the gap);
 *   - the rect is shrunk to fit that screen (a window saved on a large
 *     display must not stay oversized when restored on a small one);
 *   - if less than a grabbable slice would be visible, or the title bar
 *     would land above the available area, the rect is translated minimally
 *     back inside — position is preserved wherever it is already usable.
 *
 *  Pure function of its inputs plus the current screen layout: safe to unit
 *  test and reusable by any window-recovery path.
 * @param[in] saved Client rectangle in Qt logical screen pixels; invalid returned unchanged.
 * @param[in] titleBarStrip Pixels of top frame allowance, negative uses 28, otherwise raw no upper
 *   clamp; callers provide sensible nonnegative value.
 * @return Client rect sized/repositioned for availableGeometry of max frame-overlap screen, first
 *   tie winner; primary fallback for no overlap. No screens/target returns input unchanged. Model
 *   only top decoration, no left/right/bottom border/DPR conversion.
 * @note Preserve usable partially offscreen position (minimum 160x60 slice bounded by frame size
 *   and reachable top); oversize/title-unreachable/insufficient-visible translates minimally fully
 *   inside after shrink. Not guarantee all body pixels visible when already usable. Screen layout
 *   read only, no window/settings mutation.
 */
QRect clampToVisibleScreen(const QRect &saved, int titleBarStrip = -1);

/*! \brief Move/resize \p widget's window so it is reachable on a connected
 *  screen, using the live frame geometry to size the title-bar allowance.
 *
 *  With \p preferred non-null the window is additionally CENTERED on that
 *  screen unconditionally — the "Reset Window Positions" recovery path,
 *  which gathers every open window onto the main window's display.
 *  No-op when \p preferred is null and the window is already well placed.
 *
 *  \note Only the title-bar strip above the client rect is modelled; left,
 *  right and bottom frame borders (Windows/Linux) are not, so a restored
 *  window can still sit a few border pixels past those edges.
 * @param[in] widget Optional borrowed widget; operate on its containing window, null no-op.
 * @param[in] preferred Optional borrowed connected screen, caller preserves lifetime; nonnull
 *   unconditionally centers after size cap, even already visible elsewhere.
 * @note Derive live title allowance or fallback 28, clamp current client rect, optionally cap to
 *   preferred available size/min client height 1 and center with reachable title. Call setGeometry
 *   only if changed; widget layout/move/resize events may fire. No settings/engine write, screen
 *   ownership transfer or window activation.
 */
void ensureWindowOnScreen(QWidget *widget, const QScreen *preferred = nullptr);

/*! \brief Step H — pin the dialog above the main window so a map click
 *  doesn't hide it. Idempotent. Harmless on modal dialogs and on dialogs
 *  that already have the flag set. On macOS this is a no-op (the always-on-top
 *  hint would also float above other applications; Qt::Tool already keeps the
 *  dialog above the main window).
 * @param[in] d Optional borrowed dialog, null no-op.
 * @note macOS no-op; other platforms set WindowStaysOnTopHint true regardless modality/visibility.
 *   No native child attachment, mode preference lookup or explicit re-show; Qt flag setter window
 *   effects follow QWidget behavior. GUI thread, no ownership transfer.
 */
void applyAlwaysOnTopPolicy(QDialog *d);

/*! \brief Window flags for a modeless "floating panel" dialog that must stay
 *  above the application's OWN windows but never above other applications.
 *   macOS: a plain Qt::Dialog window (no Qt::Tool — that would hide the dialog
 *          when the app is deactivated; no WindowStaysOnTopHint — that floats
 *          above every app system-wide). The "stay above the main window"
 *          behaviour is instead provided by attaching the dialog as an NSWindow
 *          child window (see openswmmvis::platform::attachAsChildWindow, driven
 *          from the app-wide show event filter).
 *   Windows/X11: Qt::Tool + WindowStaysOnTopHint keeps the dialog above the
 *          main window (it does not leak across applications there).
 * @return Qt::Dialog on macOS, Qt::Tool|WindowStaysOnTopHint elsewhere. Flag value only, no window
 *   creation/native attachment, activation/persistence or guarantee external window-manager
 *   behavior.
 */
inline Qt::WindowFlags floatingPanelFlags()
{
#ifdef Q_OS_MACOS
    return Qt::Dialog;
#else
    return Qt::Tool | Qt::WindowStaysOnTopHint;
#endif
}

/*! \brief Just the "keep above the app's other windows" hint, for full
 *  top-level (Qt::Window) dialogs that should NOT be turned into Tool panels.
 *  Empty on macOS — those dialogs are kept above the main window by the
 *  NSWindow child-window attachment instead; WindowStaysOnTopHint elsewhere.
 * @return Empty flags macOS, WindowStaysOnTopHint elsewhere, no Tool bit. Does not set flags/create
 *   window/attach native child itself.
 */
inline Qt::WindowFlags stayAboveAppFlags()
{
#ifdef Q_OS_MACOS
    return Qt::WindowFlags();
#else
    return Qt::WindowStaysOnTopHint;
#endif
}

} // namespace openswmmvis::ui

#endif // OPENSWMMVIS_UI_DIALOGS_DIALOGLAYOUTPERSISTENCE_H
