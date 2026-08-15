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

/*! \brief Detach \p dialog from its parent NSWindow (inverse of
 *  attachAsChildWindow).
 *
 *  AppKit requires a child window to be removed from its parent *before* it
 *  is ordered out or closed — Qt's cocoa backend knows nothing about the
 *  attachment and calls orderOut:/close directly, which leaves the parent
 *  window with a stale child registration (phantom click-eating regions,
 *  broken key-window routing). Call this when the dialog is about to hide or
 *  close. If the dialog is already hidden (detach arrived after Qt's
 *  orderOut:), the window is ordered out again after detaching so it can't
 *  linger on screen glued to the parent.
 *
 *  Idempotent. No-op when \p dialog is not a window, has no realised native
 *  window, or is not currently attached. Only defined on macOS — callers
 *  must guard the call with Q_OS_MACOS. */
void detachFromParentWindow(QWidget *dialog);

} // namespace openswmmvis::platform

#endif // OPENSWMMVIS_PLATFORM_MACOSWINDOWUTILS_H
