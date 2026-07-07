/*!
 * \file   macoswindowutils.h
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  macOS-only helper to keep a dialog ordered above the application's
 *         own windows without floating above other applications.
 */
#ifndef OPENSWMMVIS_PLATFORM_MACOSWINDOWUTILS_H
#define OPENSWMMVIS_PLATFORM_MACOSWINDOWUTILS_H

class QWidget;

namespace openswmmvis::platform {

/*! \brief Attach \p dialog as an NSWindow child window of its top-level
 *  parent's window.
 *
 *  A child window stays ordered above its parent (so a click on the main
 *  window can't bury the dialog) but keeps its own normal window level — so
 *  when OpenSWMM is deactivated the dialog drops behind the other application's
 *  windows instead of floating over them, and it stays visible rather than
 *  hiding. This is the macOS-native equivalent of the always-on-top hint,
 *  scoped to the application's own windows.
 *
 *  Idempotent (safe to call again on re-show). No-op when \p dialog is not a
 *  window, has no top-level parent, or the native windows aren't realised yet.
 *  Only defined on macOS — callers must guard the call with Q_OS_MACOS. */
void attachAsChildWindow(QWidget *dialog);

} // namespace openswmmvis::platform

#endif // OPENSWMMVIS_PLATFORM_MACOSWINDOWUTILS_H
