/*!
 * \file   macoswindowutils.mm
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  macOS implementation of attachAsChildWindow (see header).
 */
#include "platform/macoswindowutils.h"

#include <QDialog>
#include <QWidget>

#import <AppKit/AppKit.h>

namespace openswmmvis::platform {

namespace {

/*! Resolve the window a dialog should be ordered above.
 *
 *  AppKit child windows move RIGIDLY with their parent, so attaching a dialog
 *  to another dialog glues the two together — that is the "moving the profile
 *  dialog also moves the time-series dialog" defect. Dialogs are frequently
 *  Qt-parented to another dialog on purpose (lifetime coupling: the overlay
 *  ComparisonPlotDialog and the various Display Options dialogs must die with
 *  the plot they configure), so the Qt parent is the wrong thing to attach to.
 *
 *  Walk up the chain of top-level windows until a NON-dialog one is found —
 *  in practice the main window / project window. Every modeless dialog then
 *  hangs off that single window and they no longer drag each other around,
 *  while still being ordered above the application's own windows.
 *
 *  Returns nullptr when the chain contains nothing but dialogs; not attaching
 *  at all is preferable to re-creating the gluing. */
QWidget *stackingHostFor_(QWidget *dialog)
{
    QWidget *parent = dialog ? dialog->parentWidget() : nullptr;
    QWidget *top    = parent ? parent->window() : nullptr;

    // Bounded walk — a pathological parent cycle must not spin forever.
    for (int hops = 0; top && hops < 16; ++hops) {
        if (top == dialog)
            return nullptr;                     // self-parented; nothing to do
        if (!qobject_cast<QDialog *>(top))
            return top;                         // real host window found
        QWidget *next = top->parentWidget();
        top = next ? next->window() : nullptr;  // dialog → keep climbing
    }
    return nullptr;
}

}   // namespace

void attachAsChildWindow(QWidget *dialog)
{
    if (!dialog || !dialog->isWindow())
        return;

    QWidget *parentTop = stackingHostFor_(dialog);
    if (!parentTop || parentTop == dialog)
        return;

    // winId() returns the backing NSView* on macOS (creating the native window
    // if needed); its .window is the NSWindow we stack.
    NSView *childView  = reinterpret_cast<NSView *>(dialog->winId());
    NSView *parentView = reinterpret_cast<NSView *>(parentTop->winId());
    if (!childView || !parentView)
        return;

    NSWindow *childWindow  = childView.window;
    NSWindow *parentWindow = parentView.window;
    if (!childWindow || !parentWindow || childWindow == parentWindow)
        return;

    // Already correctly parented → nothing to do (idempotent on re-show).
    if (childWindow.parentWindow == parentWindow)
        return;

    // Re-home if it was a child of some other window.
    if (childWindow.parentWindow)
        [childWindow.parentWindow removeChildWindow:childWindow];

    // Ordered above the parent: the child tracks the parent and stays above it,
    // but keeps its own (normal) window level, so it drops behind other apps'
    // windows when OpenSWMM is deactivated instead of floating over them.
    [parentWindow addChildWindow:childWindow ordered:NSWindowAbove];
}

void detachFromParentWindow(QWidget *dialog)
{
    if (!dialog || !dialog->isWindow())
        return;

    // internalWinId() (unlike winId()) does not force native-window creation —
    // a dialog that never realised a platform window was never attached.
    if (!dialog->internalWinId())
        return;

    NSView *childView = reinterpret_cast<NSView *>(dialog->winId());
    if (!childView)
        return;

    NSWindow *childWindow = childView.window;
    if (!childWindow || !childWindow.parentWindow)
        return;

    [childWindow.parentWindow removeChildWindow:childWindow];

    // orderOut: on an attached child window is unreliable — if Qt already
    // hid the widget before this detach ran, re-order-out now that the
    // window stands alone so it can't stay glued on screen over the parent.
    if (!dialog->isVisible() && childWindow.visible)
        [childWindow orderOut:nil];
}

} // namespace openswmmvis::platform
