#include "ui/mdiworkspacechrome.h"
#include "ui/theme/thememanager.h"
#include "ui/theme/themetokens.h"

#include <QMdiArea>
#include <QMdiSubWindow>
#include <QObject>

namespace openswmmvis::ui {

void installMdiWorkspaceChrome(QMdiArea *area)
{
    if (!area)
        return;

    // Backdrop follows the theme. setBackground() is what QMdiArea's
    // paintEvent fills the viewport with; re-running it on themeChanged
    // covers the Preferences → Appearance switch as well as the initial
    // apply() (which lands after the SWMMVis ctor has already run).
    //
    // Read the token, NOT area->palette(): QApplication::setPalette only
    // updates qApp's palette synchronously and *posts* the
    // ApplicationPaletteChange to widgets (qapplication.cpp:1750-1759), so
    // inside this handler the widget palette still holds the outgoing
    // scheme. Same approach as profileplotwidget.cpp.
    const auto syncBackdrop = [area] {
        area->setBackground(ThemeManager::instance()->colors().surfaceWindow);
    };
    syncBackdrop();
    QObject::connect(ThemeManager::instance(), &ThemeManager::themeChanged,
                     area, syncBackdrop);

    // Re-assert the maximized state Qt only propagates from a visible,
    // maximized predecessor. isHidden() (not isVisible()) is the test: it
    // is false for a sub-window that is merely waiting on its ancestors to
    // be shown, and true only for one hide()n in its own right.
    QObject::connect(area, &QMdiArea::subWindowActivated, area,
                     [area](QMdiSubWindow *sub) {
                         if (sub && !sub->isHidden()
                             && area->viewMode() == QMdiArea::TabbedView
                             && !sub->isMaximized())
                             sub->showMaximized();
                     });
}

}   // namespace openswmmvis::ui
