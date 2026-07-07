/*!
 * \file   dialoglayoutpersistence.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  Step E.1 + Step H — free helpers for dialog geometry / splitter
 *         persistence via QSettings and a uniform always-on-top policy.
 *
 * Usage (per-dialog, in the ctor after the UI is built):
 *
 *   setObjectName(QStringLiteral("MyDialog"));
 *   m_splitter->setObjectName(QStringLiteral("main"));     // any splitter
 *   if (!openswmmvis::ui::restoreDialogLayout(this)) {
 *       // first-open: apply hard-coded default sizes here
 *   }
 *   openswmmvis::ui::applyAlwaysOnTopPolicy(this);
 *
 * And in a closeEvent override:
 *
 *   openswmmvis::ui::saveDialogLayout(this);
 *
 * QSettings keys live under `Dialogs/<objectName>/...` so they're isolated
 * from main-window state and from one another.
 */
#ifndef OPENSWMMVIS_UI_DIALOGS_DIALOGLAYOUTPERSISTENCE_H
#define OPENSWMMVIS_UI_DIALOGS_DIALOGLAYOUTPERSISTENCE_H

#include <QtGlobal>            // Q_OS_MACOS
#include <QtCore/qnamespace.h> // Qt::WindowFlags, Qt::Tool, Qt::WindowStaysOnTopHint

class QDialog;
class QWidget;

namespace openswmmvis::ui {

/*! \brief Persist the dialog's geometry + every named QSplitter under it.
 *
 *  Walks `root->findChildren<QSplitter*>()` and stores each splitter's
 *  `saveState()` under `Dialogs/<root-objectName>/splitter/<splitter-objectName>`.
 *  Splitters without an `objectName` are skipped (forces deliberate opt-in).
 *
 *  No-op when \p root has no objectName (we'd have nowhere to file it).
 *  Safe to call on any QWidget — passes through to its window. */
void saveDialogLayout(QWidget *root);

/*! \brief Inverse of saveDialogLayout — restore geometry + splitter states.
 *
 *  Restored geometry is clamped to currently-available screen real estate
 *  so a saved position on a now-disconnected monitor doesn't render
 *  off-screen.
 *
 *  \returns true if any state was restored (caller can skip its defaults);
 *           false on first-open (caller applies its hard-coded defaults). */
bool restoreDialogLayout(QWidget *root);

/*! \brief Step H — pin the dialog above the main window so a map click
 *  doesn't hide it. Idempotent. Harmless on modal dialogs and on dialogs
 *  that already have the flag set. On macOS this is a no-op (the always-on-top
 *  hint would also float above other applications; Qt::Tool already keeps the
 *  dialog above the main window). */
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
 *          main window (it does not leak across applications there). */
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
 *  NSWindow child-window attachment instead; WindowStaysOnTopHint elsewhere. */
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
