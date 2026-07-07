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

} // namespace openswmmvis::platform
