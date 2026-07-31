/*!
 * \file   macoswindowutils.mm
 * \author Caleb Buahin <caleb.buahin@gmail.com>
 * \date   2026
 * \license GPL-3.0-or-later
 * \brief  macOS implementation of attachAsChildWindow (see header).
 */
#include "platform/macoswindowutils.h"

#include <QWidget>

#import <AppKit/AppKit.h>

namespace openswmmvis::platform {

void attachAsChildWindow(QWidget *dialog)
{
    if (!dialog || !dialog->isWindow())
        return;

    QWidget *parentWidget = dialog->parentWidget();
    QWidget *parentTop = parentWidget ? parentWidget->window() : nullptr;
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
